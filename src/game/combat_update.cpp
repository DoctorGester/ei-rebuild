// Combat, sixth part: the controller's update (CUnitServer slot 6, called once per world update for
// every creature on the server) and the animation queue and order states it drives. See combat.h,
// combat_plan.h and docs/notes/combat.md ("Update").
//
// The controller's animation queue is the AnimationSteps at +0x24 (steps +0x28, count +0x2C) with
// the game time its first step started at +0x38; a hit reaction saves it to +0x148 (its time to
// +0x15C) and sets +0x144 until it is restored. The plan of the order being carried out is the
// OrderPlan at +0xC4; the motion (an AIPath) is at +0x3C.
#include "ai_alarm.h"
#include "ai_map.h"
#include "ai_motion.h"
#include "combat.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "base_manager.h"
#include "core_transform.h"
#include "game_lists.h"
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

#include "lib/crt.h"

#include "ei/hook.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <bit>
#include <cstdint>
#include <cstring>

void UpdateMotion(CUnitServer*);
namespace {

// The controller's state (+0x164). combat_tick.cpp names 6 "chasing" (the state an order leaves
// the unit in when it ends: ready for the next one).
constexpr int kStateIdle = 0;
constexpr int kStateMoving = 1;
constexpr int kStateTurning = 2;
constexpr int kStateRecovering = 3;   // the rest of an action's animation plays out
constexpr int kStateWaiting = 4;
constexpr int kStateActing = 5;       // the planned action (+0xC4) runs
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
constexpr std::uint32_t kSpellNoTarget = 0x20000000;  // spell +0x3C
constexpr int kCastEffectKind = 0x4A;                 // the cast's particle effect (world slot 23)
constexpr float kNoRange = std::bit_cast<float>(0x7E967699u);   // 1e38: OrderPlan.rangeSq of no plan
constexpr int kBoredTicks = 500;                      // updates before a bored remark may come
constexpr std::uint32_t kBoredChance = 0x3E9;         // one in 1001
constexpr int kRemarkBored = 0x29;

// --- Helpers --------------------------------------------------------------------------------------

// 0073C548: how much faster a dead body sinks every update.
float SinkAcceleration() { return 0.01f /*0073C548*/; }
int TickOf(const CWorld* world) { return world->tick_; }
int WorldTime(const CEngineObject* object) { return TickOf(object->WorldContext()); }
// The connection of a unit's player (CPlayer::connection_; a CPlayerServer on the server).
CPlayerConnection* ConnectionOf(CPlayer* player) { return player->connection_; }

AnimationSteps& Queue(CUnitServer* self) { return self->queue; }
AnimationSteps& SavedQueue(CUnitServer* self) { return self->savedQueue; }
int& QueueTime(CUnitServer* self) { return Queue(self).extra; }       // +0x38
int& SavedQueueTime(CUnitServer* self) { return SavedQueue(self).extra; }   // +0x15C
OrderPlan& Plan(CUnitServer* self) { return self->currentPlan; }

// A queue that has no start time yet starts now.
void StartQueueIfUnset(CUnitServer* self) {
    if (QueueTime(self) == -1) QueueTime(self) = TickOf(ObjectWorld(self));
}

// The effect object (+0x17C, referenced) leaves the world.
void DropEffectObject(CUnitServer* self) {
    if (!self->effectObject) return;
    TheServerWorld()->RemoveTimedObject(self->effectObject->id_);
    CEngineObject* effect = self->effectObject;
    self->effectObject = nullptr;
    if (effect) effect->Release();
}

// The weapon slot the unit wants (+0x270) taken up when it differs from the active one.
void SwitchWeaponIfWanted(CUnit* unit) {
    if (unit->pendingWeapon != static_cast<int>(unit->activeWeapon))
        ServerSlots(unit->controller)->TakeUpWeapon(unit->pendingWeapon);
}

// The unit's standing flag (CUnit + 0x278) changed, re-registered on the sector map.
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
    order->flag18 = 0;
    order->param = 0;
    order->modifier = 0;
}

// An animation list on the stack (00557B60 with the derived vtable; 00557E70 at the end).
struct LocalSteps {
    AnimationSteps steps;
    LocalSteps() {
        steps.ConstructBase();
        steps.vtable = mfc::impl::AnimationStepsCopyClass::Vtable();
    }
    ~LocalSteps() { steps.DestructSteps(); }
    LocalSteps(const LocalSteps&) = delete;
    LocalSteps& operator=(const LocalSteps&) = delete;
};

// A copy of the plan's animation (00556870) with its extra dword, destroyed at the end.
struct PlanCopy {
    AnimationSteps steps;
    explicit PlanCopy(const AnimationSteps& from) {
        steps.CopyConstructSteps(from);
        steps.extra = from.extra;
    }
    ~PlanCopy() { steps.DestructSteps(); }
    PlanCopy(const PlanCopy&) = delete;
    PlanCopy& operator=(const PlanCopy&) = delete;
};

// The queue replaced by `steps`, twice as the original does, then started.
void TakeQueue(CUnitServer* self, const AnimationSteps* steps) {
    Queue(self).Assign(*steps);   // 005483C0 (twice, as the original)
    Queue(self).Assign(*steps);
    StartQueueIfUnset(self);
    self->PopFinishedSteps();
    self->ShowCurrentStep();
}

// Whether a step is open-ended (a negative duration).
bool HasOpenStep(const AnimationSteps& steps) {
    for (int i = 0; i < steps.size; ++i)
        if (steps.data[i].duration < 0) return true;
    return false;
}

// The sum of the steps' durations, -1 when one is open-ended (negative).
int TotalDuration(const AnimationSteps& steps) {
    int total = 0;
    for (int i = 0; i < steps.size; ++i) {
        if (steps.data[i].duration < 0) return -1;
        total += steps.data[i].duration;
    }
    return total;
}

} // namespace

// --- The update ---------------------------------------------------------------------------------

int CUnitServer::UpdateServer() {
    CUnit* self = unit;
    if (self->dead == 0) {
        if (ServerStatsOf(self)->Regenerate(&stats, self, action.kind != 0 ? 1 : 0)) {
            stats.Recompute(ServerStatsOf(unit), unit);
            unit->MarkChanged(0);
            unit->MarkChanged(4);
        }
        // Every (speedClass + 1) updates the unit's step noise (005C9770 returns 0: no record).
        if ((static_cast<std::uint32_t>(TheServerWorld()->tick_) & static_cast<std::uint32_t>(speedClass)) == 0) {
            AIEventPresence*& presence = aiPresence;
            if (presence) {
                presence->x = unit->localPosition_.x;
                presence->y = unit->localPosition_.y;
            }
            presence = TheServerWorld()->AIEvents()->StepNoise(&unit->localPosition_, this);
        }
        TickItemSpells();
        if (unit->player != nullptr) {   // 005301A0
            // A party unit: its perception memory, and now and then a bored remark.
            Brain()->memory.MeasureExposure(this, unit->levels4C8);
            unit->MarkChanged(0xC);
            int& bored = boredTicks;
            ++bored;
            if (bored > kBoredTicks && IsaacRandom::Global().Next() % kBoredChance == 0) {
                CPlayer* player = unit->player;
                if (!player || player->Server()->refreshPending_ != 0) {
                    bored = 0;
                } else {
                    PlayerMessage(ConnectionOf(player), unit, kRemarkBored);
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
    if (reported == 0 && Queue(this).size == 0) {
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
EI_REWRITTEN(0x00552300, CUnitServer::UpdateServer);

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
        if (Queue(this).size != 0) {
            animationHeld = 1;
            hitDirection = 0xFFFFFFFFu;
            return;
        }
        Queue(this).CopySteps(SavedQueue(this));
        QueueTime(this) = SavedQueueTime(this);
        Queue(this).CopySteps(SavedQueue(this));
        QueueTime(this) = SavedQueueTime(this);
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
        if (Queue(this).size == 0) {
            state = kStateChasing;
            unit->UpdateSizeClass();
            DropEffectObject(this);
            using Callback = void(__fastcall*)(CUnit*);
            if (auto callback = reinterpret_cast<Callback>(actionDone)) {
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
EI_REWRITTEN(0x005499F0, CUnitServer::UpdateState);

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
        if (queue.size == 0) {
            unit->SetMotionMode(0);
            return;
        }
        const AnimationStep& head = queue.data[0];
        if (head.animation != 0xFF || head.variant != 0xFF)
            unit->SetAnimationEntries(head.animation, head.variant, QueueTime(this), head.flags);
    }
    if (queue.size == 0) {
        unit->SetMotionMode(0);
        return;
    }
    int kind = queue.data[0].kind;
    if (kind == 1) {
        ++QueueTime(this);
        if (queue.size != 0) --queue.data[0].duration;
    }
    unit->SetMotionMode(kind);
}
EI_REWRITTEN(0x00546F30, CUnitServer::AdvanceAnimation);

bool CUnitServer::PopFinishedSteps() {
    bool popped = false;
    int now = WorldTime(this);
    while (queue.size != 0) {
        AnimationStep* steps = queue.data;
        int duration = steps[0].duration;
        if (duration == -1) break;
        int end = QueueTime(this) + duration;
        if (end > now) break;
        popped = true;
        if (queue.size > 1 && steps[1].animation == 0xFF && steps[1].variant == 0xFF) {
            // The pause after it goes on with this step's animation, and lasts as long more.
            steps[1].animation = steps[0].animation;
            queue.data[1].variant = queue.data[0].variant;
            queue.data[1].duration += steps[0].duration;
            if (queue.data[1].kind == 1) {
                int late = now - QueueTime(this) - steps[0].duration;
                if (late > steps[0].duration) late = steps[0].duration;
                if (late > 0) QueueTime(this) += late;
            }
        } else {
            QueueTime(this) = end;
        }
        if (queue.size - 1 != 0)
            std::memmove(queue.data, queue.data + 1, static_cast<std::size_t>(queue.size - 1) * sizeof(AnimationStep));
        --queue.size;
    }
    return popped;
}
EI_REWRITTEN(0x00546B40, CUnitServer::PopFinishedSteps);

void CUnitServer::ShowCurrentStep() {
    if (queue.size == 0) return;
    const AnimationStep& head = queue.data[0];
    if (head.animation != 0xFF || head.variant != 0xFF)
        unit->SetAnimationEntries(head.animation, head.variant, QueueTime(this), head.flags);
}
EI_REWRITTEN(0x00546C10, CUnitServer::ShowCurrentStep);

void CUnitServer::PlaySteps(const AnimationSteps* steps) {
    // The steps are copied in twice (each time resized first), as the original does.
    for (int pass = 0; pass < 2; ++pass) {
        queue.SetSize(steps->size, -1);
        for (int i = 0; i < steps->size; ++i) queue.data[i] = steps->data[i];
        QueueTime(this) = steps->extra;
    }
    StartQueueIfUnset(this);
    PopFinishedSteps();
    if (queue.size == 0) return;
    const AnimationStep& head = queue.data[0];
    if (head.animation != 0xFF || head.variant != 0xFF)
        unit->SetAnimationEntries(head.animation, head.variant, QueueTime(this), head.flags);
}
EI_REWRITTEN(0x00546C40, CUnitServer::PlaySteps);

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
EI_REWRITTEN(0x00547090, CUnitServer::SetState);

void CUnitServer::SetStanding(int standing) { SetStandingFlag(this, standing); }
EI_REWRITTEN(0x005467B0, CUnitServer::SetStanding);

void CUnitServer::StandStill() {
    motion.Clear();
    int now = TickOf(ObjectWorld(this));
    // The unit's forward direction (its rotation's Y axis, inlined), reversed.
    Vector3 direction = -RotatedAxisY(unit->localRotation_);
    if (MotionPath()->Start(&unit->localPosition_, &direction, now)) unit->RefreshMotion();
    SetStandingFlag(this, 0);
}
EI_REWRITTEN(0x00547280, CUnitServer::StandStill);

// --- Hit reactions ------------------------------------------------------------------------------

void CUnitServer::PlayHitReaction() {
    LocalSteps reaction;
    if (moveMode.mode == 0 && state == kStateIdle) return;
    BuildDeathAnimation(&reaction.steps, hitDirection, 0, 0);
    int duration = TotalDuration(reaction.steps);
    if (action.kind == kOrderAttack && state == kStateTurning &&
        TickOf(ObjectWorld(this)) + duration > nextAttackTime)
        return;
    keepHitDirection = 1;
    SavedQueue(this).CopySteps(Queue(this));
    SavedQueueTime(this) = QueueTime(this);
    Queue(this).CopySteps(reaction.steps);
    QueueTime(this) = reaction.steps.extra;
    Queue(this).CopySteps(reaction.steps);
    QueueTime(this) = reaction.steps.extra;
    StartQueueIfUnset(this);
    PopFinishedSteps();
    ShowCurrentStep();
    animationHeld = 1;
}
EI_REWRITTEN(0x00547100, CUnitServer::PlayHitReaction);

// --- The end of an order ------------------------------------------------------------------------

void CUnitServer::EndOrder(char abort) {
    int now = WorldTime(this);
    bool open = HasOpenStep(Queue(this));
    if (abort != 0 || now < Plan(this).startTime || open) {
        SetState(kStateChasing);
    } else {
        // A three-step plan (step out, action, step back): what is left of it is played out.
        if (Plan(this).animation.size == 3) {
            if (Queue(this).size == 2) {
                PlanCopy rest(Plan(this).animation);
                AnimationStep* steps = rest.steps.data;
                if (steps[1].kind == 1) {
                    rest.steps.extra = now - steps[0].duration;
                    steps[0].duration += steps[2].duration;
                    rest.steps.SetSize(1, -1);
                    TakeQueue(this, &rest.steps);
                }
            } else if (Queue(this).size == 3) {
                PlanCopy rest(Plan(this).animation);
                AnimationStep* steps = rest.steps.data;
                if (steps[1].kind == 1) {
                    steps[0].duration += steps[2].duration;
                    rest.steps.extra = QueueTime(this);
                    rest.steps.SetSize(1, -1);
                    TakeQueue(this, &rest.steps);
                } else {
                    rest.steps.extra = QueueTime(this);
                    rest.steps.RemoveAt(1, 1);
                    PlaySteps(&rest.steps);
                }
            }
        }
        SetState(kStateRecovering);
    }
    OrderPlanReset(&Plan(this));
    StandStill();
}
EI_REWRITTEN(0x00549690, CUnitServer::EndOrder);

// --- Acting -------------------------------------------------------------------------------------

void CUnitServer::TickAction() {
    CWorldServer* world = ServerWorld();
    int now = TickOf(world);
    if (now == actingCheckTime) unit->UpdateSizeClass();
    OrderPlan& plan = Plan(this);
    if (now == plan.startTime) {
        // The action starts: its animation replaces the queue, and a cast shows its effect.
        Queue(this).CopySteps(plan.animation);
        QueueTime(this) = plan.animation.extra;
        Queue(this).Assign(plan.animation);   // 005483C0
        StartQueueIfUnset(this);
        PopFinishedSteps();
        ShowCurrentStep();
        if (plan.isCast != 0 && unit->GetPrototypeModel()->castEffect != 0) {
            // The world's timed object manager (+0x48) creates the effect.
            auto* objects = TheServerWorld()->manager48_;
            CEngineObject* effect = objects->CreateObject(kCastEffectKind, -1);
            static constexpr int kEffects[8] = {0x2027, 0x2028, 0x2029, 0x202B, 0x202A, 0x202D, 0x202E, 0x202C};
            int kind = plan.castKind;
            auto* particle = static_cast<CEffectParticle*>(effect);
            particle->type_ = (kind >= 0 && kind <= 7) ? kEffects[kind] : 0x200B;
            particle->size_ = 1.0f;
            static_cast<CEffectParticle*>(effect)->SetAttachedTo(unit->id_);
            static_cast<CWorldServer*>(world)->AddTimedObject(effect);
            CEngineObject* old = effectObject;
            effectObject = effect;
            if (effect) ++effect->refCount_;
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
    // The action's moment has come: it is carried out and the plan cleared.
    FinishAction(plan.hitLocation, plan.notSkill);
    plan.animation.SetSize(0, -1);
    plan.animation.extra = -1;
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
EI_REWRITTEN(0x00549160, CUnitServer::TickAction);

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
EI_REWRITTEN(0x00549D40, CUnitServer::ActionInReach);

// --- Item spells --------------------------------------------------------------------------------

void CUnitServer::TickItemSpells() {
    CWorldServer* world = ServerWorld();
    bool healed = false;
    for (ArmourSpellList::Node* node = armourSpells.head; node;) {
        ArmourSpell* current = &node->data;
        node = node->next;
        if (current->countdown > 0) {
            --current->countdown;
            continue;
        }
        CItem* item = current->item;
        if (!item) continue;
        auto* spell = item->spell_;
        float cost = spell->manaCost_;
        if (cost >= item->charge_) continue;   // FCOMPP: C0 when the charge is above the cost (or NaN)
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
EI_REWRITTEN(0x00552B90, CUnitServer::TickItemSpells);

namespace {
// The lists RefreshItemSpells uses: two CPtrLists the unit fills with its items (vtables 0073BE80
// and 0073F12C, blocks of 10; 005316F0) and the armour spell list (+0x91C, ArmourSpellList).
using UnitItems = mfc::CTypedPtrList<CItem>;
using SpellList = mfc::CTypedPtrList<CSpell>;
using SpellEntry = ArmourSpellList::Node;
constexpr int kClassWeapon = 0x11;
constexpr std::uint32_t kSpellPeriodic = 0x20000;   // spell +0x3C
// 005316F0 CUnit: its items into two lists; 005318B0 CUnit::GetWeapon; the list instantiations
// 00558C50 / 00429F10 (their destructors), 00558B10 / 00558B80 (NewNode / FreeNode of the spell
// list), 00558840 (its element's assignment), 00429B50 (a reference's assignment).
} // namespace

void CUnitServer::RefreshItemSpells() {
    UnitItems items{mfc::impl::ItemListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
    SpellList others{mfc::impl::LootListClass::Vtable(), nullptr, nullptr, 0, nullptr, nullptr, 10};
    unit->CollectItems(&items, &others);
    // Weapons other than the one in use do not count.
    for (UnitItems::Node* node = items.head; node;) {
        UnitItems::Node* current = node;
        node = node->next;
        CItem* item = current->data;
        if (item->GetClassId() != kClassWeapon || item == unit->GetWeapon(unit->activeWeapon)) continue;
        if (current == items.head) items.head = current->next;
        else current->prev->next = current->next;
        if (current == items.tail) items.tail = current->prev;
        else current->next->prev = current->prev;
        items.FreeNode(current);
    }
    ArmourSpellList* list = &armourSpells;
    SpellEntry*& head = armourSpells.head;
    SpellEntry*& tail = armourSpells.tail;
    for (SpellEntry* entry = head; entry; entry = entry->next) entry->data.worn = 0;
    // Each item with a periodic spell keeps its entry or gets a new one. A missing item reuses the
    // previous item's spell (as the original does).
    CSpell* spell = nullptr;
    for (UnitItems::Node* node = items.head; node;) {
        CItem* item = node->data;
        node = node->next;
        if (item) spell = item->spell_;
        if (!spell || (spell->targetFlags_ & kSpellPeriodic) == 0) continue;
        SpellEntry* found = nullptr;
        for (SpellEntry* entry = head; entry; entry = entry->next) {
            if (entry->data.worn == 0 && entry->data.item == item) {
                found = entry;
                break;
            }
        }
        if (found) {
            found->data.worn = 1;
            continue;
        }
        ArmourSpell blank{};
        SpellEntry* added = list->NewNode(tail, nullptr);
        added->data.Assign(&blank);
        if (tail) tail->next = added;
        else head = added;
        tail = added;
        CItem* previous = added->data.item;   // a reference's assignment (00429B50)
        added->data.item = item;
        if (item) ++item->refCount_;
        if (previous) previous->Release();
        added->data.worn = 1;
    }
    // Entries of items no longer worn go.
    for (SpellEntry* entry = head; entry;) {
        SpellEntry* current = entry;
        entry = entry->next;
        if (current->data.worn != 0) continue;
        if (current == head) head = current->next;
        else current->prev->next = current->next;
        if (current == tail) tail = current->prev;
        else current->next->prev = current->prev;
        mfc::impl::FreeNodeAs<gamelists::Holders>(list, current);   // 00558B80
    }
    others.vtable = mfc::impl::LootListClass::Vtable();
    others.RemoveAll();
    items.vtable = mfc::impl::ItemListClass::Vtable();
    items.RemoveAll();
}
EI_REWRITTEN(0x005528F0, CUnitServer::RefreshItemSpells);
