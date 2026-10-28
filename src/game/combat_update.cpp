// Combat, sixth part: the controller's update (called once per world update for every creature on the server)
// and the animation queue and order states it drives. See docs/notes/combat.md ("Update").
#include "ai_alarm.h"
#include "ai_map.h"
#include "ai_motion.h"
#include "combat.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "base_manager.h"
#include "core_transform.h"
#include "item.h"
#include "map_object.h"
#include "player_messages.h"
#include "random.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"
#include "world_effect.h"

#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <list>

void UpdateMotion(CUnitServer*);
namespace {

// The controller's state. 6 "chasing": the state an order leaves the unit in when it ends, ready for the next
// one.
constexpr int kStateIdle = 0;
constexpr int kStateMoving = 1;
constexpr int kStateTurning = 2;
constexpr int kStateRecovering = 3;   // the rest of an action's animation plays out
constexpr int kStateWaiting = 4;
constexpr int kStateActing = 5;       // the planned action runs
constexpr int kStateChasing = 6;
constexpr int kOrderMove = 1;
constexpr int kOrderAttack = 3;
constexpr int kOrderCastAt = 4;
constexpr int kOrderCast = 5;
constexpr int kOrderSkill = 6;
constexpr int kOrderNone = 9;
constexpr int kOrderUse = 10;
constexpr std::uint32_t kSkillOnTarget = 0x18A95;
constexpr std::uint32_t kFlagFrozen = 0x1000000;      // CUnit flags_: no updates of orders and animation
constexpr std::uint32_t kFlagNoCasting = 0x10000000;  // CUnit flags_
constexpr std::uint32_t kFlagSeen8000 = 0x8000;       // CUnit flags_, cleared every update
constexpr std::uint32_t kSpellNoTarget = 0x20000000;  // CSpell::targetFlags_
constexpr int kCastEffectKind = 0x4A;                 // the cast's particle effect
constexpr float kNoRange = std::bit_cast<float>(0x7E967699u);   // 1e38: OrderPlan.rangeSq of no plan
constexpr int kBoredTicks = 500;                      // updates before a bored remark may come
constexpr std::uint32_t kBoredChance = 0x3E9;         // one in 1001
constexpr int kRemarkBored = 0x29;

// --- Helpers --------------------------------------------------------------------------------------

// How much faster a dead body sinks every update.
float SinkAcceleration() { return 0.01f /*0073C548*/; }
int TickOf(const CWorld* world) { return world->tick_; }
int WorldTime(const CEngineObject* object) { return TickOf(object->WorldContext()); }
// A CPlayerServer on the server.
CPlayerConnection* ConnectionOf(CPlayer* player) { return player->connection_; }

AnimationSteps& Queue(CUnitServer* self) { return self->queue; }
AnimationSteps& SavedQueue(CUnitServer* self) { return self->savedQueue; }
int& QueueTime(CUnitServer* self) { return Queue(self).extra; }
int& SavedQueueTime(CUnitServer* self) { return SavedQueue(self).extra; }
OrderPlan& Plan(CUnitServer* self) { return self->currentPlan; }

void StartQueueIfUnset(CUnitServer* self) {
    if (QueueTime(self) == -1) QueueTime(self) = TickOf(ObjectWorld(self));
}

void DropEffectObject(CUnitServer* self) {
    if (!self->effectObject) return;
    TheServerWorld()->RemoveTimedObject(self->effectObject->id_);
    CEngineObject* effect = self->effectObject;
    self->effectObject = nullptr;
    if (effect) effect->Release();
}

// The weapon slot the unit wants taken up when it differs from the active one.
void SwitchWeaponIfWanted(CUnit* unit) {
    if (unit->pendingWeapon != static_cast<int>(unit->activeWeapon))
        unit->Controller()->ShowWeapon(static_cast<std::uint32_t>(unit->pendingWeapon));
}

// The unit's standing flag changed, re-registered on the sector map.
void SetStandingFlag(CUnitServer* self, int standing) {
    CWorldServer* world = self->ServerWorld();
    CUnit* unit = self->unit;
    if (static_cast<int>(unit->obstacle) == standing) return;
    CSectorMap* sectors = world->Sectors();
    sectors->Remove(unit);
    self->unit->obstacle = static_cast<std::uint32_t>(standing);
    sectors->Add(self->unit);
    self->unit->MarkChanged(7);
}

// A UnitAction emptied as the original inlines it: kind 9, the target released, flags cleared.
void EmptyAction(UnitAction* order) {
    order->kind = kOrderNone;
    if (order->target) order->target->Release();
    order->target = nullptr;
    order->pointIsUseSpot = 0;
    order->param = 0;
    order->modifier = 0;
}

// The queue replaced by `steps`, then started.
void TakeQueue(CUnitServer* self, const AnimationSteps* steps) {
    Queue(self) = *steps;
    StartQueueIfUnset(self);
    self->PopFinishedSteps();
    self->ShowCurrentStep();
}

bool HasOpenStep(const AnimationSteps& steps) {
    for (const AnimationStep& step : steps)
        if (step.duration < 0) return true;
    return false;
}

int TotalDuration(const AnimationSteps& steps) {
    int total = 0;
    for (const AnimationStep& step : steps) {
        if (step.duration < 0) return -1;
        total += step.duration;
    }
    return total;
}

} // namespace

// --- The update ---------------------------------------------------------------------------------

int CUnitServer::RunTick() {
    CUnit* self = unit;
    if (self->dead == 0) {
        if (ServerStatsOf(self)->Regenerate(&stats, self, action.kind != 0 ? 1 : 0)) {
            stats.Recompute(ServerStatsOf(unit), unit);
            unit->MarkChanged(0);
            unit->MarkChanged(4);
        }
        // Every (speedClass + 1) updates the unit's step noise (it returns 0: no record).
        if ((static_cast<std::uint32_t>(TheServerWorld()->tick_) & static_cast<std::uint32_t>(speedClass)) == 0) {
            AIEventPresence*& presence = aiPresence;
            if (presence) {
                presence->x = unit->localPosition_.x;
                presence->y = unit->localPosition_.y;
            }
            presence = TheServerWorld()->AIEvents()->StepNoise(&unit->localPosition_, this);
        }
        TickItemSpells();
        if (unit->player != nullptr) {
            // A party unit: its perception memory, and now and then a bored remark.
            Brain()->memory.MeasureExposure(this, unit->levels4C8);
            unit->MarkChanged(0xC);
            int& bored = boredTicks;
            ++bored;
            if (bored > kBoredTicks && g_random.Next() % kBoredChance == 0) {
                CPlayer* player = unit->player;
                if (!player || player->Server()->refreshPending_ != 0) {
                    bored = 0;
                } else {
                    PlayerMessage(player->Server(), unit, kRemarkBored);
                    bored = 400;
                }
            }
        }
        voice.Step(this);
        brain.Think();
        unit->flags_ &= ~kFlagSeen8000;
        PropagateHostility();
        if ((unit->flags_ & kFlagFrozen) == 0 && keepHitDirection == 0 && state >= 0 &&
            (state <= kStateTurning || state == kStateChasing)) {
            UpdateOrders();
            UpdateState();
            AdvanceAnimation();
            return 1;
        }
        if (state == kStateActing && action2.kind == kOrderMove) EndOrder(1);
        UpdateState();
        AdvanceAnimation();
        return 1;
    }

    // Dead: the body sinks (accelerating) until it is at height 0; once the death animation is
    // over, the death is reported and the orders are dropped.
    CWorldServer* world = ServerWorld();
    if (self->localPosition_.z > 0.0f) {
        float& sinking = sinkSpeed;
        sinking += SinkAcceleration();
        self->localPosition_.z = self->localPosition_.z - sinking;
        float z = unit->localPosition_.z;
        unit->localPosition_.z = z <= 0.0f ? 0.0f : z;   // a NaN z is kept
        unit->MarkChanged(2);
    }
    std::uint8_t& reported = deathReported;
    if (reported == 0 && Queue(this).empty()) {
        reported = 1;
        world->TellPartiesGone(unit);
        CPlayer* player = unit->player;
        unit->SetPlayer(nullptr);
        if (player && player->IsLeader(unit) != 0) {
            ConnectionOf(player)->LeaderDied();
        } else {
            brain.Reset();
            voice.Reset();
        }
        EmptyAction(&action);
        EmptyAction(&action2);
    }
    AdvanceAnimation();
    return 1;
}

// --- The state of the unit's animation --------------------------------------------------------

void CUnitServer::UpdateState() {
    animationHeld = 0;
    frozen = 0;
    if (state == kStateActing && !ActionInReach()) EndOrder(0);
    if (unit->flags_ & kFlagFrozen) {
        frozen = 1;
        hitDirection = 0xFFFFFFFFu;
        return;
    }
    if (keepHitDirection != 0) {
        // A hit reaction plays: when it is over, the saved queue comes back.
        if (!Queue(this).empty()) {
            animationHeld = 1;
            hitDirection = 0xFFFFFFFFu;
            return;
        }
        Queue(this) = SavedQueue(this);
        StartQueueIfUnset(this);
        PopFinishedSteps();
        ShowCurrentStep();
        keepHitDirection = 0;
    }
    switch (state) {
    case kStateIdle:
        SwitchWeaponIfWanted(unit);
        if (motionChanged == 0 ||
            PathTo(&motion.goal, motion.goal.z)) {
            UpdateMotion(this);
        }
        break;
    case kStateMoving:
        SwitchWeaponIfWanted(unit);
        UpdateMotion(this);
        break;
    case kStateTurning:
    case kStateWaiting:
        SwitchWeaponIfWanted(unit);
        if (hitDirection != 0xFFFFFFFFu) PlayHitReaction();
        break;
    case kStateRecovering:
        if (Queue(this).empty()) {
            state = kStateChasing;
            unit->UpdateSizeClass();
            DropEffectObject(this);
            if (auto callback = actionDone) {
                callback(unit);
                UpdateOrders();
            }
        }
        break;
    case kStateActing:
        TickAction();
        break;
    default:
        break;
    }
    hitDirection = 0xFFFFFFFFu;
}

// --- The animation queue ---------------------------------------------------------------------

void CUnitServer::AdvanceAnimation() {
    if (frozen != 0) {
        // Frozen: everything waits one more update.
        unit->SetPathTimeShift(1);
        unit->SetMotionMode(1);
        motion.ShiftTime(1);
        ++QueueTime(this);
        ++SavedQueueTime(this);
        ++Plan(this).startTime;
        ++Plan(this).endTime;
        ++unit->animationStart;
        return;
    }
    bool held = animationHeld != 0;
    if (held) {
        motion.ShiftTime(1);
        ++SavedQueueTime(this);
        ++Plan(this).startTime;
        ++Plan(this).endTime;
    }
    unit->SetPathTimeShift(held ? 1 : 0);
    if (PopFinishedSteps()) {
        if (queue.empty()) {
            unit->SetMotionMode(0);
            return;
        }
        const AnimationStep& head = queue[0];
        if (head.animation != 0xFF || head.variant != 0xFF)
            unit->SetAnimationEntries(head.animation, head.variant, QueueTime(this), head.flags);
    }
    if (queue.empty()) {
        unit->SetMotionMode(0);
        return;
    }
    int kind = queue[0].kind;
    if (kind == 1) {
        ++QueueTime(this);
        if (!queue.empty()) --queue[0].duration;
    }
    unit->SetMotionMode(kind);
}

bool CUnitServer::PopFinishedSteps() {
    bool popped = false;
    int now = WorldTime(this);
    while (!queue.empty()) {
        AnimationStep* steps = queue.data();
        int duration = steps[0].duration;
        if (duration == -1) break;
        int end = QueueTime(this) + duration;
        if (end > now) break;
        popped = true;
        if (std::ssize(queue) > 1 && steps[1].animation == 0xFF && steps[1].variant == 0xFF) {
            // The pause after it goes on with this step's animation, and lasts as long more.
            steps[1].animation = steps[0].animation;
            queue[1].variant = queue[0].variant;
            queue[1].duration += steps[0].duration;
            if (queue[1].kind == 1) {
                int late = now - QueueTime(this) - steps[0].duration;
                if (late > steps[0].duration) late = steps[0].duration;
                if (late > 0) QueueTime(this) += late;
            }
        } else {
            QueueTime(this) = end;
        }
        queue.erase(queue.begin());
    }
    return popped;
}

void CUnitServer::ShowCurrentStep() {
    if (queue.empty()) return;
    const AnimationStep& head = queue[0];
    if (head.animation != 0xFF || head.variant != 0xFF)
        unit->SetAnimationEntries(head.animation, head.variant, QueueTime(this), head.flags);
}

void CUnitServer::PlaySteps(const AnimationSteps* steps) {
    queue = *steps;
    StartQueueIfUnset(this);
    PopFinishedSteps();
    if (queue.empty()) return;
    const AnimationStep& head = queue[0];
    if (head.animation != 0xFF || head.variant != 0xFF)
        unit->SetAnimationEntries(head.animation, head.variant, QueueTime(this), head.flags);
}

// --- States -------------------------------------------------------------------------------------

void CUnitServer::SetState(int newState) {
    state = newState;
    if (newState != kStateActing) {
        unit->UpdateSizeClass();
        DropEffectObject(this);
        return;
    }
    actingCheckTime = WorldTime(this) + 10;
}

void CUnitServer::SetStanding(int standing) { SetStandingFlag(this, standing); }

void CUnitServer::StandStill() {
    motion.Clear();
    int now = TickOf(ObjectWorld(this));
    // The unit's forward direction (its rotation's Y axis, inlined), reversed.
    Vector3 direction = -RotatedAxisY(unit->localRotation_);
    if (MotionPath()->Start(&unit->localPosition_, &direction, now)) unit->RefreshMotion();
    SetStandingFlag(this, 0);
}

// --- Hit reactions ------------------------------------------------------------------------------

void CUnitServer::PlayHitReaction() {
    AnimationSteps reaction;
    if (moveMode.mode == 0 && state == kStateIdle) return;
    BuildDeathAnimation(&reaction, hitDirection, 0, 0);
    int duration = TotalDuration(reaction);
    if (action.kind == kOrderAttack && state == kStateTurning &&
        TickOf(ObjectWorld(this)) + duration > nextAttackTime)
        return;
    keepHitDirection = 1;
    SavedQueue(this) = Queue(this);
    Queue(this) = reaction;
    StartQueueIfUnset(this);
    PopFinishedSteps();
    ShowCurrentStep();
    animationHeld = 1;
}

// --- The end of an order ------------------------------------------------------------------------

void CUnitServer::EndOrder(char abort) {
    int now = WorldTime(this);
    bool open = HasOpenStep(Queue(this));
    if (abort != 0 || now < Plan(this).startTime || open) {
        SetState(kStateChasing);
    } else {
        // A three-step plan (step out, action, step back): what is left of it is played out.
        if (std::ssize(Plan(this).animation) == 3) {
            if (Queue(this).size() == 2) {
                AnimationSteps rest = Plan(this).animation;
                if (rest[1].kind == 1) {
                    rest.extra = now - rest[0].duration;
                    rest[0].duration += rest[2].duration;
                    rest.resize(1);
                    TakeQueue(this, &rest);
                }
            } else if (Queue(this).size() == 3) {
                AnimationSteps rest = Plan(this).animation;
                if (rest[1].kind == 1) {
                    rest[0].duration += rest[2].duration;
                    rest.extra = QueueTime(this);
                    rest.resize(1);
                    TakeQueue(this, &rest);
                } else {
                    rest.extra = QueueTime(this);
                    rest.erase(rest.begin() + 1);
                    PlaySteps(&rest);
                }
            }
        }
        SetState(kStateRecovering);
    }
    OrderPlanReset(&Plan(this));
    StandStill();
}

// --- Acting -------------------------------------------------------------------------------------

void CUnitServer::TickAction() {
    CWorldServer* world = ServerWorld();
    int now = TickOf(world);
    if (now == actingCheckTime) unit->UpdateSizeClass();
    OrderPlan& plan = Plan(this);
    if (now == plan.startTime) {
        // The action starts: its animation replaces the queue, and a cast shows its effect.
        Queue(this) = plan.animation;
        StartQueueIfUnset(this);
        PopFinishedSteps();
        ShowCurrentStep();
        if (plan.isCast != 0 && unit->GetPrototypeModel()->castEffect != 0) {
            auto* objects = TheServerWorld()->manager48_;
            auto* particle = objects->CreateObjectAs<CEffectParticle>(kCastEffectKind, -1);
            static constexpr int kEffects[8] = {0x2027, 0x2028, 0x2029, 0x202B, 0x202A, 0x202D, 0x202E, 0x202C};
            int kind = plan.castKind;
            particle->type_ = (kind >= 0 && kind <= 7) ? kEffects[kind] : 0x200B;
            particle->size_ = 1.0f;
            particle->SetAttachedTo(unit->id_);
            world->AddTimedObject(particle);
            CEngineObject* old = effectObject;
            effectObject = particle;
            ++particle->refCount_;
            if (old) old->Release();
        }
    }
    if (now < plan.endTime) {
        // Still acting: the unit stays where its motion puts it, facing its direction.
        CWorld* context = WorldContext();
        Vector3 position = {unit->localPosition_.x, unit->localPosition_.y, unit->localPosition_.z};
        Vector3 direction;
        float speed;
        int cellX;
        int cellY;
        bool turning;
        unit->Motion()->Sample(context, OrderOf(MotionPath()), static_cast<float>(TickOf(context)),
                               &turning, &position.x, &direction.x, &speed, &cellX, &cellY);
        SetStandingFlag(this, state == kStateIdle ? 1 : 0);
        unit->TurnUpTowards(direction, 0);
        return;
    }
    FinishAction(plan.hitLocation, plan.notSkill);
    plan.animation = {};
    plan.targetId = -1;
    plan.endTime = 0;
    plan.startTime = 0;
    plan.hitLocation = 1;
    plan.rangeSq = kNoRange;
    plan.isCast = 0;
    plan.notSkill = 0;
    plan.castKind = 8;
    if (state == kStateActing) {
        state = kStateRecovering;
        unit->UpdateSizeClass();
        DropEffectObject(this);
    }
    motion.Clear();
    int time = TickOf(ObjectWorld(this));
    Vector3 axis;
    unit->localRotation_.AxisY(&axis);
    Vector3 backwards = -axis;
    if (MotionPath()->Start(&unit->localPosition_, &backwards, time)) unit->RefreshMotion();
    SetStanding(0);
}

// --- Checks -------------------------------------------------------------------------------------

bool CUnitServer::ActionInReach() {
    int kind = action.kind;
    bool cast = kind == kOrderCastAt || kind == kOrderCast;
    if (cast && (unit->flags_ & kFlagNoCasting)) return false;
    if (kind == kOrderCastAt || kind == kOrderUse) return true;
    if (kind != kOrderAttack && kind != kOrderSkill && kind != kOrderCast) return false;
    CMapObject* target = action.target;
    if (cast && (unit->flags_ & kFlagNoCasting)) return false;
    if (!target) {
        if (!cast) return false;
        CSpell* spell = SpellOfSource(action.source);
        if (!spell) return false;
        if ((spell->targetFlags_ & kSpellNoTarget) == 0) return false;
        // Original bug kept: with no target this reads the target's vtable (null).
    } else if (target->IsDead() != 0) {
        if (kind != kOrderSkill) return false;
        if (action.source != kSkillOnTarget && action.source != 0) return false;
    }
    Vector3 where;
    const float* at = &target->PositionAt(&where, Plan(this).endTime)->x;
    float dx = at[0] - unit->localPosition_.x;
    float dy = at[1] - unit->localPosition_.y;
    float distanceSq = LengthSquared2DUnrounded(dx, dy);
    EI_NAN_AUDIT(distanceSq, Plan(this).rangeSq);
    return distanceSq < Plan(this).rangeSq;   // closer
}

// --- Item spells --------------------------------------------------------------------------------

void CUnitServer::TickItemSpells() {
    CWorldServer* world = ServerWorld();
    bool healed = false;
    for (auto node = armourSpells.begin(); node != armourSpells.end();) {
        ArmourSpell* current = &*node;
        ++node;
        if (current->countdown > 0) {
            --current->countdown;
            continue;
        }
        CItem* item = current->item;
        if (!item) continue;
        auto* spell = item->spell_;
        float cost = spell->manaCost_;
        if (cost >= item->charge_) continue;   // a NaN charge or cost goes on
        if ((spell->prototypeId_ & 0xFFFF0000u) == 0x180000) {
            // A healing spell: only one a tick, and only when the unit is hurt.
            if (healed || unit->packedVitals.hitPoints == unit->packedVitals.maxHitPoints) continue;
            healed = true;
        }
        UseItemCharge(item, spell->manaCost_);
        CUnit* self = unit;
        world->CastSpell(spell, self, &self->localPosition_, self, &self->localPosition_, 1);
        int countdown = static_cast<int>(spell->duration_) - 3;
        current->countdown = countdown;
        if (countdown <= 0) current->countdown = 8;
    }
}

namespace {
// The lists RefreshItemSpells uses.
using UnitItems = std::list<CItem*>;
constexpr int kClassWeapon = 0x11;
constexpr std::uint32_t kSpellPeriodic = 0x20000;   // CSpell::targetFlags_
} // namespace

void CUnitServer::RefreshItemSpells() {
    UnitItems items{};
    std::list<CSpell*> others{};
    unit->CollectItems(&items, &others);
    // Weapons other than the one in use do not count.
    for (auto node = items.begin(); node != items.end();) {
        UnitItems::iterator current = node;
        ++node;
        CItem* item = *current;
        if (item->GetClassId() != kClassWeapon || item == unit->GetWeapon(unit->activeWeapon)) continue;
        items.erase(current);
    }
    ArmourSpellList* list = &armourSpells;
    for (ArmourSpell& entry : armourSpells) entry.worn = 0;
    // Each item with a periodic spell keeps its entry or gets a new one. A missing item reuses the previous
    // item's spell (like the original).
    CSpell* spell = nullptr;
    for (auto node = items.begin(); node != items.end();) {
        CItem* item = *node;
        ++node;
        if (item) spell = item->spell_;
        if (!spell || (spell->targetFlags_ & kSpellPeriodic) == 0) continue;
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
        ArmourSpell blank{};
        ArmourSpell& added = list->emplace_back();
        added.Assign(&blank);
        CItem* previous = added.item;   // a reference's assignment
        added.item = item;
        if (item) ++item->refCount_;
        if (previous) previous->Release();
        added.worn = 1;
    }
    // Entries of items no longer worn go.
    for (auto entry = armourSpells.begin(); entry != armourSpells.end();) {
        auto current = entry;
        ++entry;
        if (current->worn != 0) continue;
        if (current->item) current->item->Release();   // the node's element destroyed
        armourSpells.erase(current);
    }
    others.clear();
    items.clear();
}
