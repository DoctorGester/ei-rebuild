// Combat, sixth part: the controller's update (CUnitServer slot 6, called once per world update for
// every creature on the server) and the animation queue and order states it drives. See combat.h,
// combat_plan.h and docs/notes/combat.md ("Fourth round").
//
// The controller's animation queue is the AnimationSteps at +0x24 (steps +0x28, count +0x2C) with
// the game time its first step started at +0x38; a hit reaction saves it to +0x148 (its time to
// +0x15C) and sets +0x144 until it is restored. The plan of the order being carried out is the
// OrderPlan at +0xC4; the motion (an AIPath) is at +0x3C.
#include "combat.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "base_manager.h"
#include "item.h"
#include "map_object.h"
#include "player_messages.h"
#include "random.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"
#include "world_effect.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_core.h"
#include "originals_gameplay.h"

#include <bit>
#include <cstdint>
#include <cstring>

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

// --- Forwarders (thiscall as __fastcall with the unused EDX) --------------------------------------

// 00455940 the owning manager's context (the world server); 00455950 the object's world.
// CUnit (unit.cpp and the unit range): 0050E9F0 MarkChanged, 0052EA90 the animation bytes
// (+0x254..+0x256, time +0x258), 0052EAD0 / 0052EAF0 the dwords +0x25C / +0x260, 00530570 the
// size class, 00530970 the prototype's model record, 005300D0 SetPlayer, 0052E9D0 the path
// speeds, 00401F00 CTransformObject::TurnUpTowards. (005301A0 "has a player" is inlined.)
__declspec(naked) void __fastcall UnitSet25C(CUnit*, void*, int) EI_ORIGINAL(0x0052EAD0)
__declspec(naked) void __fastcall UnitSet260(CUnit*, void*, int) EI_ORIGINAL(0x0052EAF0)
inline void __fastcall UnitSetPlayer(CUnit* a0, void*, char* a2) { UnitSetControllerOriginal((void*)a0, nullptr, (void*)a2); }
// 0040F560 Quaternion::AxisY (core_maths.cpp).
// UnitVitals::Regenerate 00523740 and UnitStats::Recompute 00524560 (unit_vitals.cpp).
__declspec(naked) bool __fastcall VitalsRegenerate(void*, void*, UnitStats*, CUnit*, int /*inCombat*/) EI_ORIGINAL(0x00523740)
// The AI: 005C9770 the events' presence of a unit at a point (world + 0x2838; returns its record),
// 005CB850 the perception memory's update (controller + 0xA50), 005AEA80 the alarm step
// (ai_alarm.cpp, controller + 0xB10), 005AEA40 the alarm's reset, 005D53B0 AIBrain::Think and
// 005D4E80 its reset on death (unit_ai.cpp, controller + 0x980).
__declspec(naked) void* __fastcall EventsPresence(void*, void*, const float*, CUnitServer*) EI_ORIGINAL(0x005C9770)
__declspec(naked) void __fastcall MemoryUpdate(void*, void*, CUnitServer*, void*) EI_ORIGINAL(0x005CB850)
__declspec(naked) void __fastcall AlarmStep(void*, void*, CUnitServer*) EI_ORIGINAL(0x005AEA80)
__declspec(naked) void __fastcall BrainThink(void*) EI_ORIGINAL(0x005D53B0)
__declspec(naked) void __fastcall BrainReset(void*) EI_ORIGINAL(0x005D4E80)
// The world server: 00593AF0 a unit's death is final, 0058E520 / 0058E5D0 add / remove a timed
// object (world_server.cpp); 00665440 does the player still have the unit.
__declspec(naked) void __fastcall WorldUnitDied(void*, void*, CUnit*) EI_ORIGINAL(0x00593AF0)
__declspec(naked) void __fastcall WorldAddTimed(void*, void*, CEngineObject*) EI_ORIGINAL(0x0058E520)
int __fastcall PlayerHasUnit(void* a0, void*, CUnit* a2) { return PlayerOwnsUnitOriginal(a0, nullptr, a2); }
// 00679280 CEffectParticle::SetAttachedTo (world_effect_visual.cpp).
__declspec(naked) void __fastcall ParticleAttach(CEngineObject*, void*, int) EI_ORIGINAL(0x00679280)
// The sector map (world + 0x294): 005C1010 Remove, 005C0DF0 Add.
// The motion (+0x3C, an AIPath; ai_motion.cpp): 005C7CB0 ShiftTime, 005B23A0 Clear, 005C7B00
// Start, and 005C7430 UnitMotion::Sample on CUnit + 0x27C.
// The controller's functions rewritten elsewhere, through their game addresses: 0054B390
// UpdateOrders (combat_tick.cpp), 00547890 the movement tick (unit_motion_tick.cpp), 00552E00
// PropagateHostility, 00549EC0 FinishAction, 00552C60 UseItemCharge (combat_actions.cpp),
// 00550A80 BuildDeathAnimation (combat_steps.cpp), 0054B880 SpellOfSource (combat.cpp), and the
// ones not rewritten: 00548700 plan a path to a point.
__declspec(naked) void __fastcall CtlUpdateOrders(CUnitServer*) EI_ORIGINAL(0x0054B390)
__declspec(naked) void __fastcall CtlMovementTick(CUnitServer*) EI_ORIGINAL(0x00547890)
__declspec(naked) void __fastcall CtlFinishAction(CUnitServer*, void*, int, int) EI_ORIGINAL(0x00549EC0)
__declspec(naked) void* __fastcall CtlSpellOfSource(CUnitServer*, void*, std::uint32_t) EI_ORIGINAL(0x0054B880)
// The functions of this file, through their game addresses (for the verify build's wrappers).
__declspec(naked) void __fastcall CtlUpdateState(CUnitServer*) EI_ORIGINAL(0x005499F0)
__declspec(naked) void __fastcall CtlAdvanceAnimation(CUnitServer*) EI_ORIGINAL(0x00546F30)
__declspec(naked) void __fastcall CtlTickItemSpells(CUnitServer*) EI_ORIGINAL(0x00552B90)
__declspec(naked) bool __fastcall CtlActionInReach(CUnitServer*) EI_ORIGINAL(0x00549D40)
__declspec(naked) bool __fastcall CtlPopFinishedSteps(CUnitServer*) EI_ORIGINAL(0x00546B40)
__declspec(naked) void __fastcall CtlPlayHitReaction(CUnitServer*) EI_ORIGINAL(0x00547100)
__declspec(naked) void __fastcall CtlTickAction(CUnitServer*) EI_ORIGINAL(0x00549160)
// Animation lists (MFC CArray<AnimationStep> instantiations): 00556950 / 005483C0 Copy (the same
// code twice), 00556870 copy constructor, 00557B60 constructor, 00557E70 destructor, 00557B80
// SetSize, 00557E20 RemoveAt; 00544FD0 OrderPlan's reset.
AnimationSteps* __fastcall StepsCopyConstruct(AnimationSteps* a0, void*, const AnimationSteps* a2) { return (AnimationSteps*)StepsCopyConstructOriginal((void*)a0, nullptr, (const void*)a2); }
AnimationSteps* __fastcall StepsConstruct(AnimationSteps* a0) { return (AnimationSteps*)StepsConstructOriginal((void*)a0); }
inline void __fastcall StepsDestroy(AnimationSteps* s) { AnimationStepsDestroyOriginal(s); }
__declspec(naked) void __fastcall StepsRemoveAt(AnimationSteps*, void*, int, int) EI_ORIGINAL(0x00557E20)

// 0073C548: how much faster a dead body sinks every update.
float SinkAcceleration() { return *reinterpret_cast<const float*>(0x0073C548); }
int TickOf(void* world) { return static_cast<CWorld*>(world)->tick_; }
int WorldTime(const void* object) { return TickOf(ManagerContextOf(object)); }
// The connection of a unit's player (CPlayer::connection_; a CPlayerServer on the server).
CPlayerConnection* ConnectionOf(char* player) { return reinterpret_cast<CPlayer*>(player)->connection_; }

AnimationSteps& Queue(CUnitServer* self) { return self->queue; }
AnimationSteps& SavedQueue(CUnitServer* self) { return self->savedQueue; }
int& QueueTime(CUnitServer* self) { return Queue(self).extra; }       // +0x38
int& SavedQueueTime(CUnitServer* self) { return SavedQueue(self).extra; }   // +0x15C
OrderPlan& Plan(CUnitServer* self) { return self->currentPlan; }
void* Motion(CUnitServer* self) { return &self->motion; }

// A queue that has no start time yet starts now.
void StartQueueIfUnset(CUnitServer* self) {
    if (QueueTime(self) == -1) QueueTime(self) = TickOf(ObjectWorld(self));
}

// The effect object (+0x17C, referenced) leaves the world.
void DropEffectObject(CUnitServer* self) {
    if (!self->effectObject) return;
    WorldRemoveTimed(WorldServer(), nullptr, self->effectObject->id_);
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
    void* world = ManagerContextOf(self);
    CUnit* unit = self->unit;
    if (static_cast<int>(unit->unknown_278) == standing) return;
    CSectorMap* sectors = static_cast<CWorldServer*>(world)->Sectors();
    SectorMapRemove(sectors, nullptr, unit);
    self->unit->unknown_278 = static_cast<std::uint32_t>(standing);
    SectorMapAdd(sectors, nullptr, self->unit);
    UnitMarkChanged(self->unit, nullptr, 7);
}

// A UnitAction emptied as the original inlines it: kind 9, the target released, flags cleared.
void EmptyAction(UnitAction* order) {
    order->kind = kOrderNone;
    if (order->target) order->target->Release();
    order->target = nullptr;
    order->flag18 = 0;
    order->unknown_1C = 0;
    order->unknown_20 = 0;
}

// An animation list on the stack (00557B60 with the derived vtable; 00557E70 at the end).
struct LocalSteps {
    AnimationSteps steps;
    LocalSteps() {
        StepsConstruct(&steps);
        steps.vtable = kAnimationStepsVtable;
    }
    ~LocalSteps() { StepsDestroy(&steps); }
    LocalSteps(const LocalSteps&) = delete;
    LocalSteps& operator=(const LocalSteps&) = delete;
};

// A copy of the plan's animation (00556870) with its extra dword, destroyed at the end.
struct PlanCopy {
    AnimationSteps steps;
    explicit PlanCopy(const AnimationSteps& from) {
        StepsCopyConstruct(&steps, nullptr, &from);
        steps.extra = from.extra;
    }
    ~PlanCopy() { StepsDestroy(&steps); }
    PlanCopy(const PlanCopy&) = delete;
    PlanCopy& operator=(const PlanCopy&) = delete;
};

// The queue replaced by `steps`, twice as the original does, then started.
void TakeQueue(CUnitServer* self, const AnimationSteps* steps) {
    StepsCopyB(&Queue(self), nullptr, steps);
    StepsCopyB(&Queue(self), nullptr, steps);
    StartQueueIfUnset(self);
    CtlPopFinishedSteps(self);
    CtlShowCurrentStep(self);
}

// Whether a step is open-ended (a negative duration).
bool HasOpenStep(const AnimationSteps& steps) {
    for (int i = 0; i < steps.count; ++i)
        if (steps.steps[i].duration < 0) return true;
    return false;
}

// The sum of the steps' durations, -1 when one is open-ended (negative).
int TotalDuration(const AnimationSteps& steps) {
    int total = 0;
    for (int i = 0; i < steps.count; ++i) {
        if (steps.steps[i].duration < 0) return -1;
        total += steps.steps[i].duration;
    }
    return total;
}

} // namespace

// --- The update ---------------------------------------------------------------------------------

int CUnitServer::UpdateServer() {
    CUnit* self = unit;
    if (self->dead == 0) {
        if (VitalsRegenerate(ServerStatsOf(self), nullptr, &stats, self, action.kind != 0 ? 1 : 0)) {
            UnitStatsRecompute(&stats, nullptr, ServerStatsOf(unit), unit);
            UnitMarkChanged(unit, nullptr, 0);
            UnitMarkChanged(unit, nullptr, 4);
        }
        // The AI events learn where the unit is every (speedClass + 1) updates.
        if ((static_cast<std::uint32_t>(WorldServer()->tick_) & static_cast<std::uint32_t>(speedClass)) == 0) {
            AIEventPresence*& presence = unknown_918;
            if (presence) {
                presence->x = unit->position.x;
                presence->y = unit->position.y;
            }
            presence = static_cast<AIEventPresence*>(EventsPresence(WorldServer()->timeWheel_, nullptr, &unit->position.x, this));
        }
        CtlTickItemSpells(this);
        if (unit->player != nullptr) {   // 005301A0
            // A party unit: its perception memory, and now and then a bored remark.
            MemoryUpdate(Brain()->memory, nullptr, this, unit->levels4C8);
            UnitMarkChanged(unit, nullptr, 0xC);
            int& bored = unknown_938;
            ++bored;
            if (bored > kBoredTicks && IsaacRandom::Global().Next() % kBoredChance == 0) {
                char* player = unit->player;
                if (!player || reinterpret_cast<CPlayerServer*>(ConnectionOf(player))->refreshPending_ != 0) {
                    bored = 0;
                } else {
                    PlayerMessage(ConnectionOf(player), nullptr, unit, kRemarkBored);
                    bored = 400;
                }
            }
        }
        AlarmStep(voice, nullptr, this);
        BrainThink(brain);
        unit->flags_ &= ~kFlagSeen8000;
        CtlPropagateHostility(this);
        if ((unit->flags_ & kFlagFrozen) == 0 && keepHitDirection == 0 && state >= 0 &&
            (state <= kStateTurning || state == kStateChasing)) {
            CtlUpdateOrders(this);
            CtlUpdateState(this);
            CtlAdvanceAnimation(this);
            return 1;
        }
        if (state == kStateActing && action2.kind == kOrderMove) TickOrderDone(this, nullptr, 1);
        CtlUpdateState(this);
        CtlAdvanceAnimation(this);
        return 1;
    }

    // Dead: the body sinks (accelerating) until it is at height 0; once the death animation is
    // over, the death is reported and the orders are dropped.
    void* world = ManagerContextOf(this);
    if (self->position.z > 0.0f) {
        float& sinking = unknown_8F4;
        sinking += SinkAcceleration();
        self->position.z = self->position.z - sinking;
        float z = unit->position.z;
        unit->position.z = !(z <= 0.0f) ? z : 0.0f;   // FCOMP 0, z: z kept above 0 and when NaN
        UnitMarkChanged(unit, nullptr, 2);
    }
    std::uint8_t& reported = unknown_18D;
    if (reported == 0 && Queue(this).count == 0) {
        reported = 1;
        WorldUnitDied(world, nullptr, unit);
        char* player = unit->player;
        UnitSetPlayer(unit, nullptr, nullptr);
        if (player && PlayerHasUnit(player, nullptr, unit) != 0) {
            ConnectionOf(player)->LeaderDied();
        } else {
            BrainReset(brain);
            AlarmReset(voice);
        }
        EmptyAction(&action);
        EmptyAction(&action2);
    }
    CtlAdvanceAnimation(this);
    return 1;
}
EI_REPLACE_METHOD(0x00552300, CUnitServer::UpdateServer);

// --- The state of the unit's animation --------------------------------------------------------

void CUnitServer::UpdateState() {
    unknown_021 = 0;
    unknown_020 = 0;
    if (state == kStateActing && !CtlActionInReach(this)) TickOrderDone(this, nullptr, 0);
    if (unit->flags_ & kFlagFrozen) {
        unknown_020 = 1;
        hitDirection = 0xFFFFFFFFu;
        return;
    }
    if (keepHitDirection != 0) {
        // A hit reaction plays: when it is over, the saved queue comes back.
        if (Queue(this).count != 0) {
            unknown_021 = 1;
            hitDirection = 0xFFFFFFFFu;
            return;
        }
        StepsAppend(&Queue(this), nullptr, &SavedQueue(this));
        QueueTime(this) = SavedQueueTime(this);
        StepsAppend(&Queue(this), nullptr, &SavedQueue(this));
        QueueTime(this) = SavedQueueTime(this);
        StartQueueIfUnset(this);
        CtlPopFinishedSteps(this);
        CtlShowCurrentStep(this);
        keepHitDirection = 0;
    }
    switch (state) {
    case kStateIdle:
        SwitchWeaponIfWanted(unit);
        if (motionChanged == 0 ||
            TickPathTo(this, nullptr, &motion.goal.x, std::bit_cast<std::uint32_t>(motion.goal.z))) {
            CtlMovementTick(this);
        }
        break;
    case kStateMoving:
        SwitchWeaponIfWanted(unit);
        CtlMovementTick(this);
        break;
    case kStateTurning:
    case kStateWaiting:
        SwitchWeaponIfWanted(unit);
        if (hitDirection != 0xFFFFFFFFu) CtlPlayHitReaction(this);
        break;
    case kStateRecovering:
        if (Queue(this).count == 0) {
            state = kStateChasing;
            UnitUpdateSizeClass(unit);
            DropEffectObject(this);
            using Callback = void(__fastcall*)(CUnit*);
            if (auto callback = reinterpret_cast<Callback>(actionDone)) {
                callback(unit);
                CtlUpdateOrders(this);
            }
        }
        break;
    case kStateActing:
        CtlTickAction(this);
        break;
    default:
        break;
    }
    hitDirection = 0xFFFFFFFFu;
}
EI_REPLACE_METHOD(0x005499F0, CUnitServer::UpdateState);

// --- The animation queue ---------------------------------------------------------------------

void CUnitServer::AdvanceAnimation() {
    if (unknown_020 != 0) {
        // Frozen: everything waits one more update.
        UnitSet260(unit, nullptr, 1);
        UnitSet25C(unit, nullptr, 1);
        PathShiftTime(Motion(this), nullptr, 1);
        ++QueueTime(this);
        ++SavedQueueTime(this);
        ++Plan(this).startTime;
        ++Plan(this).endTime;
        ++unit->animationStart;
        return;
    }
    bool held = unknown_021 != 0;
    if (held) {
        PathShiftTime(Motion(this), nullptr, 1);
        ++SavedQueueTime(this);
        ++Plan(this).startTime;
        ++Plan(this).endTime;
    }
    UnitSet260(unit, nullptr, held ? 1 : 0);
    if (CtlPopFinishedSteps(this)) {
        if (queue.count == 0) {
            UnitSet25C(unit, nullptr, 0);
            return;
        }
        const AnimationStep& head = queue.steps[0];
        if (head.animation != 0xFF || head.unknown_01 != 0xFF)
            UnitSetAnimation(unit, nullptr, head.animation, head.unknown_01, QueueTime(this), head.unknown_02);
    }
    if (queue.count == 0) {
        UnitSet25C(unit, nullptr, 0);
        return;
    }
    int kind = queue.steps[0].unknown_08;
    if (kind == 1) {
        ++QueueTime(this);
        if (queue.count != 0) --queue.steps[0].duration;
    }
    UnitSet25C(unit, nullptr, kind);
}
EI_REPLACE_METHOD(0x00546F30, CUnitServer::AdvanceAnimation);

bool CUnitServer::PopFinishedSteps() {
    bool popped = false;
    int now = WorldTime(this);
    while (queue.count != 0) {
        AnimationStep* steps = queue.steps;
        int duration = steps[0].duration;
        if (duration == -1) break;
        int end = QueueTime(this) + duration;
        if (end > now) break;
        popped = true;
        if (queue.count > 1 && steps[1].animation == 0xFF && steps[1].unknown_01 == 0xFF) {
            // The pause after it goes on with this step's animation, and lasts as long more.
            steps[1].animation = steps[0].animation;
            queue.steps[1].unknown_01 = queue.steps[0].unknown_01;
            queue.steps[1].duration += steps[0].duration;
            if (queue.steps[1].unknown_08 == 1) {
                int late = now - QueueTime(this) - steps[0].duration;
                if (late > steps[0].duration) late = steps[0].duration;
                if (late > 0) QueueTime(this) += late;
            }
        } else {
            QueueTime(this) = end;
        }
        if (queue.count - 1 != 0)
            std::memmove(queue.steps, queue.steps + 1, static_cast<std::size_t>(queue.count - 1) * sizeof(AnimationStep));
        --queue.count;
    }
    return popped;
}
EI_REPLACE_METHOD(0x00546B40, CUnitServer::PopFinishedSteps);

void CUnitServer::ShowCurrentStep() {
    if (queue.count == 0) return;
    const AnimationStep& head = queue.steps[0];
    if (head.animation != 0xFF || head.unknown_01 != 0xFF)
        UnitSetAnimation(unit, nullptr, head.animation, head.unknown_01, QueueTime(this), head.unknown_02);
}
EI_REPLACE_METHOD(0x00546C10, CUnitServer::ShowCurrentStep);

void CUnitServer::PlaySteps(const AnimationSteps* steps) {
    // The steps are copied in twice (each time resized first), as the original does.
    for (int pass = 0; pass < 2; ++pass) {
        StepsSetSize(&queue, nullptr, steps->count, -1);
        for (int i = 0; i < steps->count; ++i) queue.steps[i] = steps->steps[i];
        QueueTime(this) = steps->extra;
    }
    StartQueueIfUnset(this);
    CtlPopFinishedSteps(this);
    if (queue.count == 0) return;
    const AnimationStep& head = queue.steps[0];
    if (head.animation != 0xFF || head.unknown_01 != 0xFF)
        UnitSetAnimation(unit, nullptr, head.animation, head.unknown_01, QueueTime(this), head.unknown_02);
}
EI_REPLACE_METHOD(0x00546C40, CUnitServer::PlaySteps);

// --- States -------------------------------------------------------------------------------------

void CUnitServer::SetState(int newState) {
    state = newState;
    if (newState != kStateActing) {
        UnitUpdateSizeClass(unit);
        DropEffectObject(this);
        return;
    }
    actingCheckTime = WorldTime(this) + 10;
}
EI_REPLACE_METHOD(0x00547090, CUnitServer::SetState);

void CUnitServer::SetStanding(int standing) { SetStandingFlag(this, standing); }
EI_REPLACE_METHOD(0x005467B0, CUnitServer::SetStanding);

void CUnitServer::StandStill() {
    PathClear(Motion(this));
    int now = TickOf(ObjectWorld(this));
    // The unit's forward direction (its rotation's Y axis, inlined), reversed.
    const Quaternion& q = unit->orientation;
    float qw = q.w, qx = q.x, qy = q.y, qz = q.z;
    float a = qx * qy - qw * qz;
    a = a + a;
    float yy = qy * qy;
    float b = (qw * qw - (qx * qx + qz * qz + yy)) + (yy + yy);
    float c = qz * qy + qw * qx;
    c = c + c;
    Vector3 direction = {-a, -b, -c};
    if (AIPathStartOriginal(MotionPath(), nullptr, &unit->position, &direction, now)) UnitPathSpeeds(unit);
    SetStandingFlag(this, 0);
}
EI_REPLACE_METHOD(0x00547280, CUnitServer::StandStill);

// --- Hit reactions ------------------------------------------------------------------------------

void CUnitServer::PlayHitReaction() {
    LocalSteps reaction;
    if (moveMode.mode == 0 && state == kStateIdle) return;
    CtlBuildDeathAnimation(this, nullptr, &reaction.steps, hitDirection, 0, 0);
    int duration = TotalDuration(reaction.steps);
    if (action.kind == kOrderAttack && state == kStateTurning &&
        TickOf(ObjectWorld(this)) + duration > nextAttackTime)
        return;
    keepHitDirection = 1;
    StepsAppend(&SavedQueue(this), nullptr, &Queue(this));
    SavedQueueTime(this) = QueueTime(this);
    StepsAppend(&Queue(this), nullptr, &reaction.steps);
    QueueTime(this) = reaction.steps.extra;
    StepsAppend(&Queue(this), nullptr, &reaction.steps);
    QueueTime(this) = reaction.steps.extra;
    StartQueueIfUnset(this);
    CtlPopFinishedSteps(this);
    CtlShowCurrentStep(this);
    unknown_021 = 1;
}
EI_REPLACE_METHOD(0x00547100, CUnitServer::PlayHitReaction);

// --- The end of an order ------------------------------------------------------------------------

void CUnitServer::EndOrder(char abort) {
    int now = WorldTime(this);
    bool open = HasOpenStep(Queue(this));
    if (abort != 0 || now < Plan(this).startTime || open) {
        CtlSetState(this, nullptr, kStateChasing);
    } else {
        // A three-step plan (step out, action, step back): what is left of it is played out.
        if (Plan(this).animation.count == 3) {
            if (Queue(this).count == 2) {
                PlanCopy rest(Plan(this).animation);
                AnimationStep* steps = rest.steps.steps;
                if (steps[1].unknown_08 == 1) {
                    rest.steps.extra = now - steps[0].duration;
                    steps[0].duration += steps[2].duration;
                    StepsSetSize(&rest.steps, nullptr, 1, -1);
                    TakeQueue(this, &rest.steps);
                }
            } else if (Queue(this).count == 3) {
                PlanCopy rest(Plan(this).animation);
                AnimationStep* steps = rest.steps.steps;
                if (steps[1].unknown_08 == 1) {
                    steps[0].duration += steps[2].duration;
                    rest.steps.extra = QueueTime(this);
                    StepsSetSize(&rest.steps, nullptr, 1, -1);
                    TakeQueue(this, &rest.steps);
                } else {
                    rest.steps.extra = QueueTime(this);
                    StepsRemoveAt(&rest.steps, nullptr, 1, 1);
                    CtlPlaySteps(this, nullptr, &rest.steps);
                }
            }
        }
        CtlSetState(this, nullptr, kStateRecovering);
    }
    PlanReset(&Plan(this));
    CtlStandStill(this);
}
EI_REPLACE_METHOD(0x00549690, CUnitServer::EndOrder);

// --- Acting -------------------------------------------------------------------------------------

void CUnitServer::TickAction() {
    void* world = ManagerContextOf(this);
    int now = TickOf(world);
    if (now == actingCheckTime) UnitUpdateSizeClass(unit);
    OrderPlan& plan = Plan(this);
    if (now == plan.startTime) {
        // The action starts: its animation replaces the queue, and a cast shows its effect.
        StepsAppend(&Queue(this), nullptr, &plan.animation);
        QueueTime(this) = plan.animation.extra;
        StepsCopyB(&Queue(this), nullptr, &plan.animation);
        StartQueueIfUnset(this);
        CtlPopFinishedSteps(this);
        CtlShowCurrentStep(this);
        if (plan.isCast != 0 && reinterpret_cast<const UnitModelRecord*>(UnitPrototypeModel(unit))->unknown_114 != 0) {
            // The world's timed object manager (+0x48) creates the effect.
            auto* objects = static_cast<CBaseManager*>(WorldServer()->manager48_);
            CEngineObject* effect = objects->CreateObject(kCastEffectKind, -1);
            static constexpr int kEffects[8] = {0x2027, 0x2028, 0x2029, 0x202B, 0x202A, 0x202D, 0x202E, 0x202C};
            int kind = plan.castKind;
            auto* particle = reinterpret_cast<CEffectParticle*>(effect);
            particle->type_ = (kind >= 0 && kind <= 7) ? kEffects[kind] : 0x200B;
            particle->size_ = 1.0f;
            ParticleAttach(effect, nullptr, unit->id_);
            WorldAddTimed(world, nullptr, effect);
            CEngineObject* old = effectObject;
            effectObject = effect;
            if (effect) ++effect->refCount_;
            if (old) old->Release();
        }
    }
    if (now < plan.endTime) {
        // Still acting: the unit stays where its motion puts it, facing its direction.
        void* context = ManagerContextOf(this);
        Vector3 position = {unit->position.x, unit->position.y, unit->position.z};
        Vector3 direction;
        float speed;
        int cellX;
        int cellY;
        bool turning;
        MotionSample(unit->motion, nullptr, context, Motion(this),
                     static_cast<float>(TickOf(context)), &turning, &position, &direction, &speed, &cellX, &cellY);
        SetStandingFlag(this, state == kStateIdle ? 1 : 0);
        TransformTurnUpTowards(reinterpret_cast<CTransformObject*>(unit), nullptr, &direction.x, 0);
        return;
    }
    // The action's moment has come: it is carried out and the plan cleared.
    CtlFinishAction(this, nullptr, plan.unknown_2C, plan.notSkill);
    StepsSetSize(&plan.animation, nullptr, 0, -1);
    plan.animation.extra = -1;
    plan.targetId = -1;
    plan.endTime = 0;
    plan.startTime = 0;
    plan.unknown_2C = 1;
    plan.rangeSq = kNoRange;
    plan.isCast = 0;
    plan.notSkill = 0;
    plan.castKind = 8;
    if (state == kStateActing) {
        state = kStateRecovering;
        UnitUpdateSizeClass(unit);
        DropEffectObject(this);
    }
    PathClear(Motion(this));
    int time = TickOf(ObjectWorld(this));
    Vector3 axis;
    QuaternionAxisY(&unit->orientation, nullptr, &axis);
    Vector3 backwards = -axis;
    if (AIPathStartOriginal(MotionPath(), nullptr, &unit->position, &backwards, time)) UnitPathSpeeds(unit);
    CtlSetStanding(this, nullptr, 0);
}
EI_REPLACE_METHOD(0x00549160, CUnitServer::TickAction);

// --- Checks -------------------------------------------------------------------------------------

bool CUnitServer::ActionInReach() {
    int kind = action.kind;
    bool cast = kind == kOrderCastAt || kind == kOrderCast;
    if (cast && (unit->flags_ & kFlagNoCasting)) return false;
    if (kind == kOrderCastAt || kind == kOrderUse) return true;
    if (kind != kOrderAttack && kind != kOrderSkill && kind != kOrderCast) return false;
    CEngineObject* target = action.target;
    if (cast && (unit->flags_ & kFlagNoCasting)) return false;
    if (!target) {
        if (!cast) return false;
        auto* spell = static_cast<CSpell*>(CtlSpellOfSource(this, nullptr, action.source));
        if (!spell) return false;
        if ((spell->targetFlags_ & kSpellNoTarget) == 0) return false;
        // Original bug kept: with no target this reads the target's vtable (null).
    } else if (ObjectSlots(target)->IsDead() != 0) {
        if (kind != kOrderSkill) return false;
        if (action.source != kSkillOnTarget && action.source != 0) return false;
    }
    Vector3 where;
    const float* at = &ObjectSlots(target)->PositionAt(&where, Plan(this).endTime)->x;
    float dx = at[0] - unit->position.x;
    float dy = at[1] - unit->position.y;
    float distanceSq = LengthSquared2DUnrounded(dx, dy);
    return !(distanceSq >= Plan(this).rangeSq);   // C0: closer, or unordered
}
EI_REPLACE_METHOD(0x00549D40, CUnitServer::ActionInReach);

// --- Item spells --------------------------------------------------------------------------------

void CUnitServer::TickItemSpells() {
    void* world = ManagerContextOf(this);
    bool healed = false;
    for (ArmourSpellList::Node* node = armourSpells.head; node;) {
        ArmourSpell* current = &node->data;
        node = node->next;
        if (current->countdown > 0) {
            --current->countdown;
            continue;
        }
        auto* item = static_cast<CItem*>(current->item);
        if (!item) continue;
        auto* spell = static_cast<CSpell*>(item->spell_);
        float cost = std::bit_cast<float>(spell->unknown_40);
        if (cost >= item->charge_) continue;   // FCOMPP: C0 when the charge is above the cost (or NaN)
        if ((spell->unknown_18 & 0xFFFF0000u) == 0x180000) {
            // A healing spell: only one a tick, and only when the unit is hurt.
            if (healed || unit->packedVitals.hitPoints == unit->packedVitals.maxHitPoints) continue;
            healed = true;
        }
        CtlUseItemCharge(this, nullptr, item, std::bit_cast<float>(spell->unknown_40));
        CUnit* self = unit;
        WorldCastSpell(world, nullptr, spell, self, &self->position.x, self, &self->position.x, 1);
        int countdown = static_cast<int>(spell->unknown_20) - 3;
        current->countdown = countdown;
        if (countdown <= 0) current->countdown = 8;
    }
}
EI_REPLACE_METHOD(0x00552B90, CUnitServer::TickItemSpells);

namespace {
// The lists RefreshItemSpells uses: two CPtrLists the unit fills with its items (vtables 0073BE80
// and 0073F12C, blocks of 10; 005316F0) and the armour spell list (+0x91C, ArmourSpellList).
using ItemList = mfc::CTypedPtrList<CEngineObject>;
using SpellEntry = ArmourSpellList::Node;
constexpr int kClassWeapon = 0x11;
constexpr std::uint32_t kSpellPeriodic = 0x20000;   // spell +0x3C
// 005316F0 CUnit: its items into two lists; 005318B0 CUnit::GetWeapon; the list instantiations
// 00558C00 (FreeNode of the first), 00558C50 / 00429F10 (their destructors), 00558B10 / 00558B80
// (NewNode / FreeNode of the spell list), 00558840 (its element's assignment), 00429B50 (a
// reference's assignment).
__declspec(naked) void __fastcall UnitCollectItems(CUnit*, void*, ItemList*, ItemList*) EI_ORIGINAL(0x005316F0)
__declspec(naked) void __fastcall ItemListFreeNode(ItemList*, void*, ItemList::Node*) EI_ORIGINAL(0x00558C00)
void __fastcall ItemListBDestroy(ItemList* a0) { ItemListRemoveAllOriginal((void*)a0); }
inline void __fastcall ItemListADestroy(ItemList* a0) { PtrListRemoveAllNodesOriginal((void*)a0); }
__declspec(naked) SpellEntry* __fastcall SpellNewNode(ArmourSpellList*, void*, SpellEntry* /*prev*/, SpellEntry* /*next*/)
    EI_ORIGINAL(0x00558B10)
__declspec(naked) void __fastcall SpellFreeNode(ArmourSpellList*, void*, SpellEntry*) EI_ORIGINAL(0x00558B80)
__declspec(naked) ArmourSpell* __fastcall SpellElementAssign(ArmourSpell*, void*, const ArmourSpell*) EI_ORIGINAL(0x00558840)
} // namespace

void CUnitServer::RefreshItemSpells() {
    ItemList items{reinterpret_cast<void*>(0x0073BE80), nullptr, nullptr, 0, nullptr, nullptr, 10};
    ItemList others{reinterpret_cast<void*>(0x0073F12C), nullptr, nullptr, 0, nullptr, nullptr, 10};
    UnitCollectItems(unit, nullptr, &items, &others);
    // Weapons other than the one in use do not count.
    for (ItemList::Node* node = items.head; node;) {
        ItemList::Node* current = node;
        node = node->next;
        CEngineObject* item = current->data;
        if (item->GetClassId() != kClassWeapon || item == UnitWeapon(unit, nullptr, unit->activeWeapon)) continue;
        if (current == items.head) items.head = current->next;
        else current->prev->next = current->next;
        if (current == items.tail) items.tail = current->prev;
        else current->next->prev = current->prev;
        ItemListFreeNode(&items, nullptr, current);
    }
    ArmourSpellList* list = &armourSpells;
    SpellEntry*& head = armourSpells.head;
    SpellEntry*& tail = armourSpells.tail;
    for (SpellEntry* entry = head; entry; entry = entry->next) entry->data.worn = 0;
    // Each item with a periodic spell keeps its entry or gets a new one. A missing item reuses the
    // previous item's spell (as the original does).
    CSpell* spell = nullptr;
    for (ItemList::Node* node = items.head; node;) {
        CEngineObject* item = node->data;
        node = node->next;
        if (item) spell = static_cast<CSpell*>(static_cast<CItem*>(item)->spell_);
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
        SpellEntry* added = SpellNewNode(list, nullptr, tail, nullptr);
        SpellElementAssign(&added->data, nullptr, &blank);
        if (tail) tail->next = added;
        else head = added;
        tail = added;
        ReferenceAssign(&added->data.item, nullptr, item);
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
        SpellFreeNode(list, nullptr, current);
    }
    others.vtable = reinterpret_cast<void*>(0x0073F12C);
    ItemListBDestroy(&others);
    items.vtable = reinterpret_cast<void*>(0x0073BE80);
    ItemListADestroy(&items);
}
EI_REPLACE_METHOD(0x005528F0, CUnitServer::RefreshItemSpells);
