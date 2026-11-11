// Combat, sixth part: the controller's update (called once per world update for every creature on the server)
// and the animation queue and order states it drives. See docs/notes/combat.md ("Update").
#include "game/ai/ai_alarm.h"
#include "game/ai/ai_map.h"
#include "game/ai/ai_motion.h"
#include "engine/core/base_manager.h"
#include "game/combat/combat.h"
#include "game/combat/combat_internal.h"
#include "engine/core/core_transform.h"
#include "game/app/singletons.h"
#include "game/items/item.h"
#include "game/objects/map_object.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "engine/core/random.h"
#include "game/ai/sector_map.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"

#include "lib/float_select.h"
#include "lib/vecmath.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <list>

void UpdateMotion(UnitServer*);
namespace {

constexpr int kBoredTicks = 500;                      // updates before a bored remark may come
constexpr std::uint32_t kBoredChance = 1001;          // one in 1001

// --- Helpers --------------------------------------------------------------------------------------

// How much faster a dead body sinks every update.
constexpr float kSinkAcceleration = 0.01f;


// The weapon slot the unit wants taken up when it differs from the active one.
void SwitchWeaponIfWanted(Unit* unit) {
    if (unit->pendingWeapon != static_cast<int>(unit->activeWeapon))
        unit->controller->ShowWeapon(static_cast<std::uint32_t>(unit->pendingWeapon));
}

// The unit's standing flag changed, re-registered on the sector map.
void SetStandingFlag(UnitServer* self, int standing) {
    WorldServer* world = self->ServerWorld();
    Unit* unit = self->unit;
    if (static_cast<int>(unit->obstacle) == standing) return;
    SectorMap* sectors = &world->aiMap.sectorMap;
    sectors->Remove(unit);
    self->unit->obstacle = static_cast<std::uint32_t>(standing);
    sectors->Add(self->unit);
    self->unit->MarkChanged(NetField::Motion);
}

bool HasOpenStep(const AnimationSteps& steps) {
    for (const AnimationStep& step : steps.steps)
        if (step.duration < 0) return true;
    return false;
}

// Whether a bored party unit makes its remark this update: one in kBoredChance, picked by a hash of the tick and
// the unit's id. The remark is only a voice, so it takes no draw from the game's generator; the client's would
// make the saved boredTicks follow the frames.
bool BoredRemarkDue(int tick, std::uint32_t id) {
    std::uint32_t h = static_cast<std::uint32_t>(tick) * 0x9E3779B1u + id;
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h % kBoredChance == 0;
}

} // namespace

void UnitServer::StartQueueIfUnset() {
    if (queue.extra == -1) queue.extra = OwningWorld()->tick;
}

void UnitServer::DropEffectObject() {
    if (!effectObject) return;
    g_serverWorld.RemoveTimedObject(effectObject->id);
    effectObject.Reset();
}

// --- The update ---------------------------------------------------------------------------------

bool UnitServer::RunTick() {
    Unit* self = unit;
    WorldServer* world = ServerWorld();
    if (self->dead == 0) {
        if (self->Vitals().Regenerate(&stats, self, action.kind != UnitOrderKind::Idle ? 1 : 0)) {
            stats.Recompute(&unit->Vitals(), unit);
            unit->MarkChanged(NetField::Vitals);
            unit->MarkChanged(NetField::Stats);
        }
        // Every (speedClass + 1) updates the unit's step noise.
        if ((static_cast<std::uint32_t>(world->tick) & static_cast<std::uint32_t>(speedClass)) == 0)
            world->aiMap.events.StepNoise(unit->localPosition, this);
        TickItemSpells();
        if (unit->player != nullptr) {
            // A party unit: its perception memory, and now and then a bored remark.
            unit->exposure = brain.memory.MeasureExposure(this);
            unit->MarkChanged(NetField::Exposure);
            ++boredTicks;
            if (boredTicks > kBoredTicks && BoredRemarkDue(world->tick, static_cast<std::uint32_t>(unit->id))) {
                Player* player = unit->player;
                if (!player || player->Server()->danger != PlayerServer::PartyDanger::Safe) {
                    boredTicks = 0;
                } else {
                    PlayerMessage(player->Server(), unit, AckKind::Bored);
                    boredTicks = 400;
                }
            }
        }
        voice.Step(this);
        brain.Think();
        unit->flags &= ~MapObjectFlag::InSectorMap;
        PropagateHostility();
        if ((unit->flags & MapObjectFlag::Frozen) == 0 && keepHitDirection == 0 &&
            (state == UnitState::Idle || state == UnitState::Moving || state == UnitState::Turning ||
             state == UnitState::Stopped)) {
            UpdateOrders();
            UpdateState();
            AdvanceAnimation();
            return true;
        }
        if (state == UnitState::Acting && action2.kind == UnitOrderKind::Move) EndOrder(1);
        UpdateState();
        AdvanceAnimation();
        return true;
    }

    // Dead: the body sinks (accelerating) until it is at height 0; once the death animation is
    // over, the death is reported and the orders are dropped.
    if (self->localPosition.z > 0.0f) {
        sinkSpeed += kSinkAcceleration;
        self->localPosition.z -= sinkSpeed;
        float z = unit->localPosition.z;
        unit->localPosition.z = MaxKeepB(0.0f, z);    // a NaN z is kept
        unit->MarkChanged(NetField::Placement);
    }
    if (deathReported == 0 && queue.steps.empty()) {
        deathReported = 1;
        world->TellPartiesGone(unit);
        Player* player = unit->player;
        unit->SetPlayer(nullptr);
        if (player && player->IsLeader(unit) != 0) {
            player->connection->LeaderDied();
        } else {
            if (player && player->Server()) player->Server()->CompanionDied(unit);
            brain.Reset();
            voice.Reset();
        }
        action.Reset();
        action2.Reset();
    }
    AdvanceAnimation();
    return true;
}

// --- The state of the unit's animation --------------------------------------------------------

void UnitServer::UpdateState() {
    animationHeld = 0;
    frozen = 0;
    if (state == UnitState::Acting && !ActionInReach()) EndOrder(0);
    if (unit->flags & MapObjectFlag::Frozen) {
        frozen = 1;
        hitDirection = kNoHitDirection;
        return;
    }
    if (keepHitDirection != 0) {
        // A hit reaction plays: when it is over, the saved queue comes back.
        if (!queue.steps.empty()) {
            animationHeld = 1;
            hitDirection = kNoHitDirection;
            return;
        }
        PlaySteps(&savedQueue);
        keepHitDirection = 0;
    }
    switch (state) {
    case UnitState::Idle:
        SwitchWeaponIfWanted(unit);
        if (motionChanged == 0 ||
            PathTo(motion.goal, motion.goal.z)) {
            UpdateMotion(this);
        }
        break;
    case UnitState::Moving:
        SwitchWeaponIfWanted(unit);
        UpdateMotion(this);
        break;
    case UnitState::Turning:
    case UnitState::Emote:
        SwitchWeaponIfWanted(unit);
        if (hitDirection != kNoHitDirection) PlayHitReaction();
        break;
    case UnitState::Animating:
        if (queue.steps.empty()) {
            state = UnitState::Stopped;
            unit->UpdateSizeClass();
            DropEffectObject();
            if (auto callback = actionDone) {
                callback(unit);
                UpdateOrders();
            }
        }
        break;
    case UnitState::Acting:
        TickAction();
        break;
    default:
        break;
    }
    hitDirection = kNoHitDirection;
}

// --- The animation queue ---------------------------------------------------------------------

void UnitServer::AdvanceAnimation() {
    if (frozen != 0) {
        // Frozen: everything waits one more update.
        unit->SetPathTimeShift(1);
        unit->SetMotionMode(1);
        motion.ShiftTime(1);
        ++queue.extra;
        ++savedQueue.extra;
        ++currentPlan.startTime;
        ++currentPlan.endTime;
        ++unit->animationStart;
        return;
    }
    bool held = animationHeld != 0;
    if (held) {
        motion.ShiftTime(1);
        ++savedQueue.extra;
        ++currentPlan.startTime;
        ++currentPlan.endTime;
    }
    unit->SetPathTimeShift(held ? 1 : 0);
    if (PopFinishedSteps()) {
        if (queue.steps.empty()) {
            unit->SetMotionMode(0);
            return;
        }
        ShowCurrentStep();
    }
    if (queue.steps.empty()) {
        unit->SetMotionMode(0);
        return;
    }
    int kind = queue.steps[0].kind;
    if (kind == 1) {
        ++queue.extra;
        if (!queue.steps.empty()) --queue.steps[0].duration;
    }
    unit->SetMotionMode(kind);
}

bool UnitServer::PopFinishedSteps() {
    bool popped = false;
    int now = OwningWorld()->tick;
    while (!queue.steps.empty()) {
        AnimationStep* steps = queue.steps.data();
        int duration = steps[0].duration;
        if (duration == -1) break;
        int end = queue.extra + duration;
        if (end > now) break;
        popped = true;
        if (std::ssize(queue.steps) > 1 && steps[1].IsBlank()) {
            // The pause after it goes on with this step's animation, and lasts as long more.
            steps[1].animation = steps[0].animation;
            queue.steps[1].variant = queue.steps[0].variant;
            queue.steps[1].duration += steps[0].duration;
            if (queue.steps[1].kind == 1) {
                int late = now - queue.extra - steps[0].duration;
                if (late > steps[0].duration) late = steps[0].duration;
                if (late > 0) queue.extra += late;
            }
        } else {
            queue.extra = end;
        }
        queue.steps.erase(queue.steps.begin());
    }
    return popped;
}

void UnitServer::ShowCurrentStep() {
    if (queue.steps.empty()) return;
    const AnimationStep& head = queue.steps[0];
    if (!head.IsBlank()) unit->SetAnimationEntries(head.animation, head.variant, queue.extra, head.flags);
}

void UnitServer::PlaySteps(const AnimationSteps* steps) {
    queue = *steps;
    StartQueueIfUnset();
    PopFinishedSteps();
    ShowCurrentStep();
}

// --- States -------------------------------------------------------------------------------------

void UnitServer::SetState(UnitState newState) {
    state = newState;
    if (newState != UnitState::Acting) {
        unit->UpdateSizeClass();
        DropEffectObject();
        return;
    }
    actingCheckTime = OwningWorld()->tick + 10;
}

void UnitServer::ClearStanding() { SetStandingFlag(this, 0); }

void UnitServer::StandStill() {
    motion.Clear();
    int now = OwningWorld()->tick;
    // The unit's forward direction (its rotation's Y axis), reversed.
    Vector3 direction = -(unit->localRotation).AxisY();
    if (motion.Start(unit->localPosition, direction, now)) unit->RefreshMotion();
    SetStandingFlag(this, 0);
}

// --- Hit reactions ------------------------------------------------------------------------------

void UnitServer::PlayHitReaction() {
    AnimationSteps reaction;
    if (moveMode.mode == MoveMode::Lie && state == UnitState::Idle) return;
    BuildDeathAnimation(&reaction, hitDirection, 0, 0);
    int duration = reaction.Duration();
    if (action.kind == UnitOrderKind::Attack && state == UnitState::Turning &&
        OwningWorld()->tick + duration > nextAttackTime)
        return;
    keepHitDirection = 1;
    savedQueue = queue;
    PlaySteps(&reaction);
    animationHeld = 1;
}

// --- The end of an order ------------------------------------------------------------------------

void UnitServer::EndOrder(char abort) {
    int now = OwningWorld()->tick;
    bool open = HasOpenStep(queue);
    if (abort != 0 || now < currentPlan.startTime || open) {
        SetState(UnitState::Stopped);
    } else {
        // A three-step plan (step out, action, step back): what is left of it is played out.
        if (std::ssize(currentPlan.animation.steps) == 3) {
            if (queue.steps.size() == 2) {
                AnimationSteps rest = currentPlan.animation;
                if (rest.steps[1].kind == 1) {
                    rest.extra = now - rest.steps[0].duration;
                    rest.steps[0].duration += rest.steps[2].duration;
                    rest.steps.resize(1);
                    PlaySteps(&rest);
                }
            } else if (queue.steps.size() == 3) {
                AnimationSteps rest = currentPlan.animation;
                if (rest.steps[1].kind == 1) {
                    rest.steps[0].duration += rest.steps[2].duration;
                    rest.extra = queue.extra;
                    rest.steps.resize(1);
                    PlaySteps(&rest);
                } else {
                    rest.extra = queue.extra;
                    rest.steps[1] = rest.steps[2];   // the middle step dropped (three steps here)
                    rest.steps.pop_back();
                    PlaySteps(&rest);
                }
            }
        }
        SetState(UnitState::Animating);
    }
    currentPlan.Reset();
    StandStill();
}

// --- Acting -------------------------------------------------------------------------------------

void UnitServer::TickAction() {
    WorldServer* world = ServerWorld();
    int now = world->tick;
    if (now == actingCheckTime) unit->UpdateSizeClass();
    OrderPlan& plan = currentPlan;
    if (now == plan.startTime) {
        // The action starts: its animation replaces the queue, and a cast shows its effect.
        PlaySteps(&plan.animation);
        if (plan.isCast != 0 && unit->GetPrototypeModel()->castEffect != 0) {
            auto* objects = g_serverWorld.timedObjectManager.get();
            auto* particle = objects->CreateObjectAs<EffectParticle>(ClassId::EffectParticle, -1);
            // The casting particle of each SpellSchool.
            static constexpr int kEffects[8] = {
                static_cast<int>(ParticleType::CastingFire),       static_cast<int>(ParticleType::CastingElectricity),
                static_cast<int>(ParticleType::CastingAcid),       static_cast<int>(ParticleType::CastingIllusion),
                static_cast<int>(ParticleType::CastingDivination), static_cast<int>(ParticleType::CastingEnchantment),
                static_cast<int>(ParticleType::CastingHealing),    static_cast<int>(ParticleType::CastingDomination)};
            int kind = static_cast<int>(plan.castKind);
            particle->type = (kind >= 0 && kind <= 7) ? kEffects[kind] : static_cast<int>(ParticleType::Casting);
            particle->size = 1.0f;
            particle->SetAttachedTo(unit->id);
            world->AddTimedObject(particle);
            effectObject.Assign(particle);
        }
    }
    if (now < plan.endTime) {
        // Still acting: the unit stays where its motion puts it, facing its direction.
        World* context = OwningWorld();
        MotionSample sample = unit->motion.Sample(context, OrderOf(&motion), static_cast<float>(context->tick));
        SetStandingFlag(this, state == UnitState::Idle ? 1 : 0);
        unit->TurnUpTowards(sample.direction, 0);
        return;
    }
    FinishAction(plan.hitLocation);
    plan.Reset();
    if (state == UnitState::Acting) {
        state = UnitState::Animating;
        unit->UpdateSizeClass();
        DropEffectObject();
    }
    StandStill();
}

// --- Checks -------------------------------------------------------------------------------------

bool UnitServer::ActionInReach() {
    UnitOrderKind kind = action.kind;
    bool cast = kind == UnitOrderKind::CastAt || kind == UnitOrderKind::CastOn;
    if (cast && (unit->flags & MapObjectFlag::NoCasting)) return false;
    if (kind == UnitOrderKind::CastAt || kind == UnitOrderKind::UseItem) return true;
    if (kind != UnitOrderKind::Attack && kind != UnitOrderKind::Use && kind != UnitOrderKind::CastOn) return false;
    MapObject* target = action.target;
    if (!target) {
        if (!cast) return false;
        Spell* spell = SpellOfSource(action.source);
        if (!spell) return false;
        if ((spell->targetFlags & SpellTargetFlag::AtPoint) == 0) return false;
    } else if (target->IsDead() != 0) {
        if (kind != UnitOrderKind::Use) return false;
        if (action.Skill() != UseSource::SearchBody && action.Skill() != UseSource::Use) return false;
    }
    // A cast at a point whose target is gone measures to the order's point.
    Vector3 at = target ? target->PositionAt(currentPlan.endTime) : action.AimPoint();
    float distanceSq = DistanceSquaredXY(at, unit->localPosition);
    return distanceSq < currentPlan.rangeSq;   // closer
}

// --- Item spells --------------------------------------------------------------------------------

void UnitServer::TickItemSpells() {
    WorldServer* world = ServerWorld();
    bool healed = false;
    // CastSpell does not reach RefreshItemSpells (the only change to the list while the unit runs), so the
    // entry stays put across the cast.
    for (ArmourSpell& entry : armourSpells) {
        if (entry.countdown > 0) {
            --entry.countdown;
            continue;
        }
        Item* item = entry.item;
        if (!item) continue;
        auto* spell = item->spell.Get();
        float cost = spell->manaCost;
        if (cost >= item->charge) continue;    // a NaN charge or cost goes on
        if (spell->GetPrototype()->itemHeals) {
            // Only one a tick, and only when the unit is hurt.
            if (healed || unit->packedVitals.hitPoints == unit->packedVitals.maxHitPoints) continue;
            healed = true;
        }
        UseItemCharge(item, spell->manaCost);
        Unit* self = unit;
        world->CastSpell(spell, self, self->localPosition, self, self->localPosition, 1);
        int countdown = static_cast<int>(spell->duration) - 3;
        entry.countdown = countdown;
        if (countdown <= 0) entry.countdown = 8;
    }
}

void UnitServer::RefreshItemSpells() {
    ItemsAndSpells held = unit->CollectItems();
    std::vector<Item*>& items = held.items;
    // Weapons other than the one in use do not count.
    std::erase_if(items, [this](Item* item) {
        return item->GetClassId() == ClassId::ItemWeapon && item != unit->GetWeapon(unit->activeWeapon);
    });
    for (ArmourSpell& entry : armourSpells) entry.worn = 0;
    // Each item with a periodic spell keeps its entry or gets a new one. A missing item reuses the previous
    // item's spell (like the original).
    Spell* spell = nullptr;
    for (Item* item : items) {
        if (item) spell = item->spell;
        if (!spell || (spell->targetFlags & SpellTargetFlag::ItemContinuous) == 0) continue;
        ArmourSpell* found = nullptr;
        for (ArmourSpell& entry : armourSpells) {
            if (entry.worn == 0 && entry.item == item) {
                found = &entry;
                break;
            }
        }
        if (found) {
            found->worn = 1;
            continue;
        }
        armourSpells.push_back({item, 0, 1});
    }
    // Entries of items no longer worn go (their items released from the head).
    for (ArmourSpell& entry : armourSpells)
        if (entry.worn == 0) entry.item.Reset();
    std::erase_if(armourSpells, [](const ArmourSpell& entry) { return entry.worn == 0; });
}
