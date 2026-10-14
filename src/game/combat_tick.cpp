// Combat, fifth part: the per-tick order machine of CUnitServer: taking over the queued order,
// carrying out the current one (closing in on the target, planning the moment of the action) and
// what is left when an order ends. See combat.h, combat_plan.h and docs/notes/combat.md ("Third
// round").
#include "combat.h"
#include "originals_combat.h"
#include "combat_plan.h"

#include "item.h"
#include "item_db.h"
#include "map_object.h"
#include "maths.h"
#include "player_messages.h"
#include "random.h"
#include "unit_ai.h"
#include "unit_db.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_units.h"
#include "originals_world.h"

#include <bit>
#include <cstring>

#pragma float_control(precise, on)

namespace {
constexpr float kZero = 0.0f;                                       // 0073B858
constexpr float kOne = 1.0f;                                        // 0073B84C / 0073EEE4
constexpr float kHalf = 0.5f;                                       // 0073B86C
constexpr float kPointOne = std::bit_cast<float>(0x3DCCCCCDu);      // 0073B850 (0.1)
constexpr float kNearPoint = std::bit_cast<float>(0x3DB851ECu);     // 0073F0B0 (0.09)
constexpr float kShort = std::bit_cast<float>(0x3F7FBE77u);         // 0073F0B4 (0.999)
constexpr float kLong = std::bit_cast<float>(0x3A83126Fu);          // 0073CCF4 (0.001)
constexpr float kFive = 5.0f;                                       // 0073BF34
constexpr float kFifteen = 15.0f;                                   // 0073C518
constexpr float kHeightLimit = 1.5f;                                // 0073C51C
constexpr double kArrived = 0.02;                                   // 0073F098
constexpr std::uint32_t kMinClosingBits = 0x3DCCCCCDu;              // 0.1: the slowest closing speed
constexpr std::uint32_t kFarBits = 0x49742400u;                     // 1e6: no limit for a path

constexpr int kOrderNone = 0;
constexpr int kOrderMove = 1;
constexpr int kOrderTurn = 2;
constexpr int kOrderAttack = 3;
constexpr int kOrderCast = 4;
constexpr int kOrderCastAt = 5;
constexpr int kOrderSkill = 6;
constexpr int kOrderAnimation = 7;
constexpr int kOrderEmote = 8;
constexpr int kOrderEmpty = 9;
constexpr int kOrderUse = 10;
constexpr int kOrderStop = 11;
constexpr std::uint32_t kSkillOnTarget = 0x18A95;
constexpr std::uint32_t kQuickItemSource = 0x80000000u;
constexpr std::uint32_t kFlagNoCasting = 0x10000000;    // CUnit flags_
constexpr std::uint32_t kSpellNoTarget = 0x20000000;    // spell +0x3C
constexpr int kStateIdle = 0;
constexpr int kStateTurning = 2;
constexpr int kStateCasting = 5;
constexpr int kStateChasing = 6;
constexpr int kModeStop = 4;
constexpr int kModeWalk = 1;
constexpr int kModeRun = 2;
constexpr int kAttackLead = 0x10;       // ticks a chase looks ahead
constexpr int kChaseMinTicks = 2;
constexpr int kChaseMaxTicks = 0x40;
constexpr int kWeaponBow = 5;
constexpr int kWeaponCrossbow = 6;

// Party messages (0066D6E0).
constexpr int kMessageCannotReach = 0x0C;
constexpr int kMessageTargetLost = 0x0B;
constexpr int kMessageCannotDo = 0x0E;
constexpr int kMessageNoMana = 0x0F;
constexpr int kMessageNoCharge = 0x11;
constexpr int kMessageUnseen = 0x12;
constexpr int kMessageStopped = 0x1B;
// Shouts (00552780).
constexpr int kShoutTargetLost = 0x0B;
constexpr int kShoutCannotDo = 0x0E;

constexpr int kPartMoveModes = 6;           // MarkChanged part of the movement modes

bool LessOrUnordered(double a, double b) { return !(a >= b); }
bool ZeroOrUnordered(double a) { return !(a < 0.0 || a > 0.0); }
bool IsUnit(CEngineObject* object) { return object && object->GetClassId() == kClassUnit; }
bool IsCastKind(int kind) { return kind == kOrderCast || kind == kOrderCastAt; }
bool IsActionKind(int kind) { return kind == kOrderAttack || kind == kOrderCast || kind == kOrderCastAt || kind == kOrderSkill; }

// The map objects' virtual slots (map_object.h). Slot 54 is 1 while a unit moves (+0x278).
CMapObjectVirtuals* Slots(CEngineObject* object) { return ObjectSlots(object); }
bool IsDead(CEngineObject* object) { return Slots(object)->IsDead() != 0; }
int IsMoving(CEngineObject* object) { return Slots(object)->IsLargeUnit(); }
// The world time of an object's world (00455950).
int WorldTick(const void* object) { return static_cast<CWorld*>(ObjectWorld(object))->tick_; }
// The prototype's height offset (UnitPrototypeRecord +0x24).
float PrototypeHeight(CUnit* unit) { return std::bit_cast<float>(reinterpret_cast<const UnitPrototypeRecord*>(unit->GetPrototype())->unknown_24); }
// The messages of a unit's player (CPlayer::connection_).
void* PlayerMessages(char* player) { return reinterpret_cast<CPlayer*>(player)->connection_; }

CItemWeapon* ActiveWeapon(CUnit* unit) { return static_cast<CItemWeapon*>(unit->GetWeapon(unit->activeWeapon)); }
} // namespace

namespace {
// Not rewritten (forwarders), as __fastcall with the unused EDX for __thiscall:
// 00455950 the object's world; 0066D6E0 party messages; 00552780 the unit shouts.
// CUnitServer helpers: 0054A150 follow the path (true while moving), 00550F30 the idle animation
// (+0x960), 00546C40 play it, 00547280 / 00547090 / 005474B0 / 0054EB30 / 005467B0 movement and
// motion updates, 00548700 plan a path to a point, 005468D0 take nearby units off the sector map
// into a list, 005494D0 start the planned action, 00548DE0 / 00548FA0 play an animation list
// (taken by value), 00549690 the end of an order.
__declspec(naked) bool __fastcall TickFollowPath(CUnitServer*) EI_ORIGINAL(0x0054A150)
__declspec(naked) void __fastcall TickMotion5474B0(CUnitServer*) EI_ORIGINAL(0x005474B0)
__declspec(naked) void __fastcall TickStartAction(CUnitServer*, void*, int, int) EI_ORIGINAL(0x005494D0)
__declspec(naked) void __fastcall TickPlayEmote(CUnitServer*, void*, AnimationSteps) EI_ORIGINAL(0x00548FA0)
__declspec(naked) bool __fastcall TickTalkTargetBusy(CUnitServer*, void*, CEngineObject*) EI_ORIGINAL(0x0054C760)
bool __fastcall TickCheckOrder(CUnitServer* a0, void*, const UnitAction* a2) { return UnitCheckOrderOriginal((void*)a0, nullptr, (const void*)a2); }
// Containers: 00544E70 the animation queue (+0x24) holds a list; 00544D50 an emote's steps;
// 00545110 the plan takes a list (by value), its start and duration; 0054B260 / 00557E70 copy /
// destroy a list; 00557B80 SetSize; 00544CE0 AddStep; 0052CA10 a CPtrList's RemoveAll.
bool __fastcall QueueHolds(void* a0, void*, AnimationSteps* a2) { return StepsQueueHoldsOriginal(a0, nullptr, (const void*)a2); }
void __fastcall StepsEmote(AnimationSteps* a0, void*, int a2) { StepsEmoteOriginal((void*)a0, nullptr, a2); }
__declspec(naked) void __fastcall PlanTake(OrderPlan*, void*, AnimationSteps, int, int) EI_ORIGINAL(0x00545110)
__declspec(naked) AnimationSteps* __fastcall StepsCopy(AnimationSteps*, void*, const AnimationSteps*) EI_ORIGINAL(0x0054B260)
inline void __fastcall StepsDestroy(AnimationSteps* s) { AnimationStepsDestroyOriginal(s); }
void __fastcall StepsAddStepTick(AnimationSteps* a0, void*, int a2, int a3) { StepsAddStepOriginal((void*)a0, nullptr, a2, a3); }
inline void __fastcall PtrListRemoveAll(void* l) { PtrListRemoveAllOriginal(l); }
// Motion (+0x3C): 005B1DE0 turn to a point, 005B23A0 stop, 005C7B00 place along a direction.
__declspec(naked) void __fastcall MotionTurn(void*, void*, int, CUnit*, const float*, int) EI_ORIGINAL(0x005B1DE0)
// CUnit: 0052E9D0 path speeds, 00530570 size class, 00530A60 IsBehind (unit_hits.cpp),
// 0052FBC0 ScaledValue (unit.cpp; ST0).
__declspec(naked) double __fastcall UnitScaledValue(CUnit*, void*, float) EI_ORIGINAL(0x0052FBC0)
// The AI map's cell flag 0x80 under a point (005BBC70), the sector map (CSectorMap::Remove /
// Add, sector_map.cpp), the world's timed objects (0058E5D0, world_server.cpp), the brain's
// leader (005D5E90, unit_ai.cpp), the memory's "is an enemy" (005CB7F0), the party's sight of a
// unit (0066E110).
int __fastcall MemoryIsEnemy(void* a0, void*, CUnit* a2, CEngineObject* a3) { return AiMemoryIsEnemyOriginal(a0, nullptr, (void*)a2, (void*)a3); }
__declspec(naked) bool __fastcall PartySees(void*, void*, CEngineObject*) EI_ORIGINAL(0x0066E110)
// Rewritten elsewhere (combat*.cpp), through their game addresses: ActionDistance, CanReachTarget,
// CanCast, BuildAttackHit, ResolveHit, AnimationDuration, BuildOrderSteps.
__declspec(naked) bool __fastcall TickCanCast(CUnitServer*, void*, const UnitAction*) EI_ORIGINAL(0x0054BF80)
__declspec(naked) BOOL __fastcall TickBuildHit(CUnitServer*, void*, DamageInfo*, int, int, char) EI_ORIGINAL(0x0054F630)
__declspec(naked) BOOL __fastcall TickResolveHit(CUnitServer*, void*, DamageInfo*, CUnit*) EI_ORIGINAL(0x00553DF0)
} // namespace

__declspec(naked) bool CUnitServer::CallStartQueuedOrder() EI_ORIGINAL(0x0054A900)
__declspec(naked) void CUnitServer::CallSyncMoveModes() EI_ORIGINAL(0x0054A620)
__declspec(naked) void CUnitServer::CallExecuteOrder() EI_ORIGINAL(0x0054D960)
__declspec(naked) void CUnitServer::CallApproachTarget() EI_ORIGINAL(0x0054D140)
__declspec(naked) void CUnitServer::CallContinueAction() EI_ORIGINAL(0x0054DD70)
__declspec(naked) void CUnitServer::CallTakeQueuedOrder() EI_ORIGINAL(0x0054A6E0)
__declspec(naked) void CUnitServer::CallSetUnitMoveMode2(int) EI_ORIGINAL(0x00546830)
__declspec(naked) UnitAction* UnitAction::CallAssign(const UnitAction*) EI_ORIGINAL(0x0054B320)
__declspec(naked) void UnitAction::CallReset() EI_ORIGINAL(0x0054B230)

namespace {
void TellParty(CUnit* unit, int message) {
    if (unit->player) PlayerSendObject0BOriginal(PlayerMessages(unit->player), nullptr, unit, message);
}

// 00545F40 (UnitAction::Clear) followed by the +0x1C / +0x20 reset, as callers inline it.
void ClearAction(UnitAction* order) {
    ActionClear(order);
    order->param = 0;
    order->modifier = 0;
}

// A CPtrList on the stack (vtable 0073D454), as 005468D0 fills it.
using UnitPtrList = mfc::CTypedPtrList<CEngineObject>;
} // namespace

// --- Small helpers -----------------------------------------------------------------------------

UnitAction* UnitAction::Assign(const UnitAction* from) {
    kind = from->kind;
    source = from->source;
    if (from->target != target) {
        if (target) target->Release();
        target = nullptr;
        if (from->target) ++from->target->refCount_;
        target = from->target;
    }
    std::memcpy(&point, &from->point, sizeof(point));
    flag18 = from->flag18;
    param = from->param;
    param = from->param;   // copied twice by the original
    modifier = from->modifier;
    return this;
}
EI_REPLACE_METHOD(0x0054B320, UnitAction::Assign);

void UnitAction::Reset() {
    kind = kOrderEmpty;
    if (target) target->Release();
    target = nullptr;
    flag18 = 0;
    param = 0;
    modifier = 0;
}
EI_REPLACE_METHOD(0x0054B230, UnitAction::Reset);

void CUnitServer::SetUnitMoveMode2(int mode) {
    if (mode == unit->moveMode2) return;
    unit->moveMode2 = mode;
    unit->MarkChanged(kPartMoveModes);
}
EI_REPLACE_METHOD(0x00546830, CUnitServer::SetUnitMoveMode2);

void CUnitServer::SyncMoveModes() {
    if (action.kind == kOrderAttack) moveMode2.flag4 = 1;
    else if (unit->player) moveMode2.flag4 = 0;
    moveMode.wasMode3 = 0;
    moveMode.mode = unit->moveMode;
    if (unit->moveMode == 3) {
        moveMode.mode = kModeRun;
        moveMode.wasMode3 = 1;
    }
    moveMode2.wasMode3 = 0;
    moveMode2.mode = unit->moveMode2;
    if (unit->moveMode2 == 3) {
        moveMode2.mode = kModeRun;
        moveMode2.wasMode3 = 1;
    }
    if (action.modifier && moveMode.mode == kModeRun) moveMode2.wasMode3 |= 2;
    if (!unit->player && moveMode2.flag4) moveMode2.wasMode3 |= 1;
}
EI_REPLACE_METHOD(0x0054A620, CUnitServer::SyncMoveModes);

namespace {
// The melee reach test of 0054C8B0 as 0054A6E0 inlines it: false when the target (a living unit
// standing on its own) is too high to reach, else whether the action can be planned (0054C6B0).
bool CanStillDo(CUnitServer* self, UnitAction* order) {
    if (order->kind == kOrderAttack || order->kind == kOrderSkill) {
        CEngineObject* target = order->target;
        if (IsUnit(target) && !IsDead(target)) {
            auto* other = static_cast<CUnit*>(target);
            if (other->sizeClass == 0 && !(order->kind == kOrderAttack && self->HasRangedWeapon())) {
                double height = static_cast<double>(PrototypeHeight(other)) + other->position.z;
                if (!LessOrUnordered(height, kHeightLimit)) return false;
            }
        }
    }
    return CtlCanPlanAction(self, nullptr, order);
}
} // namespace

void CUnitServer::TakeQueuedOrder() {
    action.kind = action2.kind;
    action.source = action2.source;
    // 0052A8F0: the counted reference assigned.
    CEngineObject* target = action2.target;
    if (target != action.target) {
        if (action.target) action.target->Release();
        action.target = nullptr;
        if (target) ++target->refCount_;
        action.target = target;
    }
    std::memcpy(&action.point, &action2.point, sizeof(action.point));
    action.flag18 = action2.flag18;
    action.param = action2.param;
    action.param = action2.param;
    action.modifier = action2.modifier;
    action2.kind = kOrderEmpty;
    if (action2.target) action2.target->Release();
    action2.target = nullptr;
    action2.flag18 = 0;
    action2.param = 0;
    action2.modifier = 0;
    if (action.modifier && moveMode.mode != kModeRun) {
        if (!(IsActionKind(action.kind) && CanStillDo(this, &action)) && unit->moveMode2 != kModeRun) {
            unit->moveMode2 = kModeRun;
            unit->MarkChanged(kPartMoveModes);
        }
    }
    CallSyncMoveModes();
    waitFrame = WorldServer()->tick_;
}
EI_REPLACE_METHOD(0x0054A6E0, CUnitServer::TakeQueuedOrder);

// --- Taking over the queued order ----------------------------------------------------------------

namespace {
// The common start of taking over the queued order: run (mode 2) unless the order can be carried
// out at once, then the movement records and the frame stamp.
void LeaveMoveMode(CUnitServer* self) {
    UnitAction* order = &self->action;
    if (order->modifier && self->moveMode.mode != kModeRun) {
        int kind = order->kind;
        if (!(IsActionKind(kind) && TickCheckOrder(self, nullptr, order))) self->CallSetUnitMoveMode2(kModeRun);
    }
    self->CallSyncMoveModes();
    self->waitFrame = WorldServer()->tick_;
}

// Stand: the idle animation (restarted at a random phase when nothing plays) and the motion
// updates. Not while turning (state 2).
void StandIdle(CUnitServer* self) {
    AnimationSteps* idle = CtlIdleAnimation(self);
    if (!QueueHolds(&self->queue, nullptr, idle)) {
        if (self->queue.count == 0 && self->queue.extra == -1) {
            int now = WorldTick(self);
            idle->extra = now - static_cast<int>(IsaacRandom::Global().Next() % 0x33);
        }
        CtlPlaySteps(self, nullptr, idle);
    }
    CtlStandStill(self);
    CtlSetState(self, nullptr, 2);
}

// The queued order replaces the current one (0054B320 then 0054B230 on the queued order).
void TakeOver(CUnitServer* self) {
    self->action.CallAssign(&self->action2);
    self->action2.CallReset();
}

void InitPtrList(UnitPtrList* list) {
    std::memset(list, 0, sizeof(*list));
    list->vtable = mfc::PtrListVtable();
    list->blockSize = 10;
}

void DestroyPtrList(UnitPtrList* list) {
    list->vtable = mfc::PtrListVtable();
    PtrListRemoveAll(list);
}
} // namespace

bool CUnitServer::StartMove() {
    void* world = ManagerContext();
    UnitPtrList lifted;
    InitPtrList(&lifted);
    orderStopped = 0;
    TakeOver(this);
    LeaveMoveMode(this);
    if (TickFollowPath(this)) {
        DestroyPtrList(&lifted);
        return true;
    }
    // Already there: the order is done and the next one starts.
    Vector3 offset;
    offset.x = unit->position.x - action.point.x;
    offset.y = unit->position.y - action.point.y;
    offset.z = unit->position.z - action.point.z;
    double lengthSq = LengthSquaredZYX(offset);
    if (LessOrUnordered(lengthSq, kArrived)) {
        action.CallReset();
        if (action2.kind == kOrderEmpty) reinterpret_cast<ControllerOriginals*>(this)->Idle();
        bool result = CallStartQueuedOrder();
        DestroyPtrList(&lifted);
        return result;
    }
    // Plan the way with the units near the goal lifted off the sector map.
    CtlLiftNearby(this, nullptr, &lifted, &action.point.x, action.source != 0);
    CAIMap* map = static_cast<CWorldServer*>(ManagerContext())->AIMap();
    unit->unknown_43C = CellFlag80AtOriginal(map, nullptr, &unit->position);
    bool found = TickPathTo(this, nullptr, &action.point.x, std::bit_cast<std::uint32_t>(action.point.z));
    for (UnitPtrList::Node* node = lifted.head; node;) {
        UnitPtrList::Node* current = node;
        node = node->next;
        SectorMapAdd(static_cast<CWorldServer*>(world)->Sectors(), nullptr, current->data);
    }
    bool arrived = false;
    if (action.param) {
        action.param = 0;
        bool close = false;
        if (!found) {
            double dy = static_cast<double>(action.point.y) - unit->position.y;
            double dx = static_cast<double>(action.point.x) - unit->position.x;
            close = LessOrUnordered(LengthSquared2DUnrounded(dx, dy), kLong);
        }
        if (close) arrived = true;
        else CtlReportPath(this, nullptr, &motion, found);
    }
    if (!arrived && found) {
        DestroyPtrList(&lifted);
        return true;
    }
    orderStopped = 1;
    action.CallReset();
    if (action2.kind == kOrderEmpty) reinterpret_cast<ControllerOriginals*>(this)->Idle();
    CallStartQueuedOrder();
    DestroyPtrList(&lifted);
    return false;
}

bool CUnitServer::StartQueuedOrder() {
    if (action2.kind == kOrderEmpty) return false;
    if (action2.kind != kOrderStop && moveMode.mode == kModeStop) {
        moveMode2.mode = kModeRun;
        unit->moveMode2 = kModeRun;
        if (TickFollowPath(this)) return true;
    }
    switch (action2.kind) {
    case kOrderNone:
        action.CallAssign(&action2);
        ClearAction(&action2);
        LeaveMoveMode(this);
        if (state != kStateTurning) StandIdle(this);
        return true;

    case kOrderMove:
        return StartMove();

    case kOrderTurn: {
        TakeOver(this);
        LeaveMoveMode(this);
        int time = action.param;
        int now = static_cast<CWorld*>(ManagerContext())->tick_;
        MotionTurn(&motion, nullptr, now, unit, &action.point.x, time);
        UnitPathSpeeds(unit);
        TickMotion5474B0(this);
        CtlSetState(this, nullptr, 1);
        return true;
    }

    case kOrderAttack:
    case kOrderCast:
    case kOrderCastAt:
    case kOrderSkill:
        orderStopped = 0;
        TakeOver(this);
        LeaveMoveMode(this);
        stoodBlocked = 0;
        CallExecuteOrder();
        return true;

    case kOrderAnimation: {
        TakeOver(this);
        LeaveMoveMode(this);
        AnimationSteps steps;
        std::memset(&steps, 0, sizeof(steps));
        steps.vtable = kAnimationStepsVtable;
        StepsSetSize(&steps, nullptr, 0, -1);
        steps.extra = -1;
        std::uint8_t animation = static_cast<std::uint8_t>(action.param);
        int duration = CtlAnimationDuration(this, nullptr, animation);
        StepsAddStepTick(&steps, nullptr, animation, duration);
        AnimationSteps copy;
        StepsCopy(&copy, nullptr, &steps);
        CtlPlayAnimationOrder(this, nullptr, copy);
        StepsDestroy(&steps);
        return true;
    }

    case kOrderEmote: {
        CallTakeQueuedOrder();
        AnimationSteps steps;
        std::memset(&steps, 0, sizeof(steps));
        steps.vtable = kAnimationStepsVtable;
        StepsSetSize(&steps, nullptr, 0, -1);
        steps.extra = -1;
        StepsEmote(&steps, nullptr, static_cast<std::uint8_t>(action.param));
        AnimationSteps copy;
        StepsCopy(&copy, nullptr, &steps);
        TickPlayEmote(this, nullptr, copy);
        StepsDestroy(&steps);
        return true;
    }

    case kOrderUse: {
        CallTakeQueuedOrder();
        AnimationSteps steps;
        std::memset(&steps, 0, sizeof(steps));
        steps.vtable = kAnimationStepsVtable;
        int duration;
        CtlBuildOrderSteps(this, nullptr, &steps, &duration, kOrderSkill, 0);
        int now = WorldTick(this);
        AnimationSteps copy;
        StepsCopy(&copy, nullptr, &steps);
        PlanTake(&currentPlan, nullptr, copy, now, duration);
        TickStartAction(this, nullptr, 1, 0);
        StepsDestroy(&steps);
        return true;
    }

    case kOrderStop:
        if (moveMode.mode != kModeStop) {
            moveMode2.mode = kModeStop;
            unit->moveMode2 = kModeStop;
            if (TickFollowPath(this)) return true;
            TellParty(unit, kMessageStopped);
            action.CallReset();
            if (action2.kind == kOrderEmpty) reinterpret_cast<ControllerOriginals*>(this)->Idle();
            return false;
        }
        TakeOver(this);
        LeaveMoveMode(this);
        if (state != kStateTurning) StandIdle(this);
        return true;

    default:
        return false;
    }
}
EI_REPLACE_METHOD(0x0054A900, CUnitServer::StartQueuedOrder);

// --- Every tick ----------------------------------------------------------------------------------

void CUnitServer::UpdateOrders() {
    if (CallStartQueuedOrder()) {
        boredTicks = 0;
        return;
    }
    CallSyncMoveModes();
    if (TickFollowPath(this)) return;
    switch (action.kind) {
    case kOrderMove:
    case kOrderTurn:
        if (state != kStateChasing) return;
        if (stoodBlocked) {
            orderStopped = 1;
            if (!BrainLeaderOrFollowedOriginal(&brain) && unit->player) TellParty(unit, kMessageCannotDo);
        }
        if (action.kind != kOrderNone && action2.kind != kOrderAnimation && action2.kind != kOrderEmote)
            DropQueuedOrder();
        break;
    case kOrderAttack:
    case kOrderCast:
    case kOrderCastAt:
    case kOrderSkill:
        CallContinueAction();
        return;
    case kOrderAnimation:
        if (action.kind != kOrderNone && action2.kind != kOrderAnimation && action2.kind != kOrderEmote)
            DropQueuedOrder();
        break;
    default:
        return;
    }
    CallStartQueuedOrder();
}
EI_REPLACE_METHOD(0x0054B390, CUnitServer::UpdateOrders);

void CUnitServer::ContinueAction() {
    CEngineObject* target = action.target;
    bool seen = true;
    char* player = unit->player;
    if (player && IsUnit(target) && !MemoryIsEnemy(Brain()->memory, nullptr, unit, target))
        seen = PartySees(PlayerMessages(player), nullptr, target);

    bool cast = IsCastKind(action.kind);
    bool carryOn = false;
    if (!(cast && (unit->flags_ & kFlagNoCasting))) {
        if (!action.target) {
            if (cast) {
                auto* spell = static_cast<CSpell*>(SpellOfSource(action.source));
                carryOn = spell && (spell->targetFlags_ & kSpellNoTarget);
            }
        } else if (!IsDead(action.target)) {
            carryOn = true;
        } else if (action.kind == kOrderSkill && (action.source == kSkillOnTarget || action.source == 0)) {
            carryOn = true;   // talk to or study the dead
        }
    }
    bool failed = false;
    if (carryOn) {
        if (stoodBlocked) {
            failed = true;
        } else if (seen) {
            CallExecuteOrder();
            return;
        }
    } else if (stoodBlocked) {
        failed = true;
    }
    if (failed) {
        orderStopped = 1;
        TellParty(unit, action.kind == kOrderAttack ? kMessageTargetLost : kMessageCannotDo);
    }
    if (!seen) {
        orderStopped = 1;
        TellParty(unit, kMessageUnseen);
    }
    action.kind = kOrderEmpty;
    if (action.target) action.target->Release();
    action.target = nullptr;
    action.flag18 = 0;
    action.param = 0;
    action.modifier = 0;
    if (action2.kind == kOrderEmpty && action.kind != kOrderNone) DropQueuedOrder();
    TickOrderDone(this, nullptr, 0);
}
EI_REPLACE_METHOD(0x0054DD70, CUnitServer::ContinueAction);

// --- Carrying out the current order ----------------------------------------------------------------

namespace {
// The unit's attack interval: the weapon prototype's +0x6C (or stats +0x8C truncated) scaled by
// 15 / stats +0x30 (unscaled when that is 0 or NaN), rounded.
int AttackInterval(CUnitServer* self, CItemWeapon* weapon) {
    volatile float factor = self->Combat().rate;
    if (!ZeroOrUnordered(factor)) factor = kFifteen / factor;
    int base;
    if (weapon) {
        base = static_cast<int>(reinterpret_cast<const itemdb::ItemRecordHead*>(weapon->GetPrototype())->category);
    } else {
        double value = self->Combat().attackCategory;
        // _ftol (006E853C), as the original truncates it.
        int truncated;
        __asm {
            fld value
            mov eax, 0x006E853C
            call eax
            mov truncated, eax
        }
        base = truncated;
    }
    volatile float scaled = static_cast<float>(static_cast<double>(base) * factor);
    return x87::fistp(scaled);
}

// 00546830 as 0054D960 inlines it.
void SetModeInline(CUnit* unit, int mode) {
    if (unit->moveMode2 == mode) return;
    unit->moveMode2 = mode;
    unit->MarkChanged(kPartMoveModes);
}

bool OutOfReachUp(CUnitServer* self, const UnitAction* order) {
    if (order->kind != kOrderAttack && order->kind != kOrderSkill) return false;
    CEngineObject* target = order->target;
    if (!IsUnit(target) || IsDead(target)) return false;
    auto* other = static_cast<CUnit*>(target);
    if (other->sizeClass != 0) return false;
    if (order->kind == kOrderAttack && self->HasRangedWeapon()) return false;
    double height = static_cast<double>(PrototypeHeight(other)) + other->position.z;
    return !LessOrUnordered(height, kHeightLimit);
}
} // namespace

bool CUnitServer::CanReachAndPlan(const UnitAction* order) {
    if (OutOfReachUp(this, order)) return false;
    return CtlCanPlanAction(this, nullptr, order);
}
EI_REPLACE_METHOD(0x0054C8B0, CUnitServer::CanReachAndPlan);

bool CUnitServer::CanReachAndPlanFrom(const UnitAction* order, const Vector3* point) {
    if (OutOfReachUp(this, order)) return false;
    // The position is swapped bitwise, as the original does.
    Vector3 saved;
    std::memcpy(&saved, &unit->position, sizeof(saved));
    std::memcpy(&unit->position, point, sizeof(saved));
    bool result = CtlCanPlanAction(this, nullptr, order);
    std::memcpy(&unit->position, &saved, sizeof(saved));
    return result;
}
EI_REPLACE_METHOD(0x0054C960, CUnitServer::CanReachAndPlanFrom);

void CUnitServer::ExecuteOrder() {
    int now = WorldTick(this);
    searchingTarget = 0;
    if (nextAttackTime > now && action.kind == kOrderAttack) {
        CallApproachTarget();
        return;
    }
    if (unit->pendingWeapon != static_cast<int>(unit->activeWeapon))
        ServerSlots(unit->controller)->TakeUpWeapon(unit->pendingWeapon);
    OrderPlan* plan = &currentPlan;
    if (!CallPlanAction(&action, plan)) {
        CallApproachTarget();
        return;
    }
    bool clear = false;
    bool stop = false;
    if (action.kind == kOrderSkill && action.source == kSkillOnTarget && TickTalkTargetBusy(this, nullptr, action.target)) {
        clear = true;
    } else {
        if (action.kind != kOrderSkill) {
            if (IsCastKind(action.kind) && unit->type_ == kUnitTypeHero) {
                if (moveMode.mode == 0) {
                    SetModeInline(unit, kModeWalk);
                    stop = true;
                }
            } else if (moveMode.mode != kModeRun) {
                SetModeInline(unit, kModeRun);
                stop = true;
            }
        }
        CallSyncMoveModes();
        if (TickFollowPath(this)) {
            searchingTarget = 1;
            return;
        }
    }

    if (!clear && !stop) {
        if (OutOfReachUp(this, &action)) {
            TellParty(unit, kMessageCannotReach);
            clear = true;
        }
    }
    if (!clear && !stop) {
        int& nextAttack = nextAttackTime;
        if (nextAttack > plan->startTime) {
            if (action.kind == kOrderAttack) {
                CallApproachTarget();
                return;
            }
        } else if (action.kind == kOrderAttack) {
            int interval = AttackInterval(this, ActiveWeapon(unit));
            int next = interval + now;
            if (nextAttack > next) next = nextAttack;
            nextAttack = next;
        }
        if (IsCastKind(action.kind) && !TickCanCast(this, nullptr, &action)) {
            TellParty(unit, (action.source & kQuickItemSource) ? kMessageNoCharge : kMessageNoMana);
            stop = true;
        } else {
            // The action starts now.
            bool fresh = true;
            CEngineObject* target = action.target;
            if (IsUnit(target)) {
                auto* other = static_cast<CUnit*>(target);
                auto* theirs = reinterpret_cast<CUnitServer*>(other->controller);
                if (reinterpret_cast<CEngineObject*>(theirs->action.target) == unit) fresh = false;
            }
            bool missed = false;
            if (action.kind == kOrderAttack && action.target->GetClassId() == kClassUnit) {
                char calledShot = static_cast<char>(plan->notSkill);
                int location = plan->hitLocation;
                auto* victim = static_cast<CUnit*>(action.target);
                auto* defender = reinterpret_cast<CUnitServer*>(victim->controller);
                ManagerContext();
                int backstab = UnitIsBehind(unit, nullptr, victim) & 0xFF;
                if (victim->GetClassId() == kClassUnit && defender->NoBackstab()) backstab = 0;
                CItemWeapon* weapon = ActiveWeapon(unit);
                if (weapon && (weapon->GetType() == kWeaponBow || weapon->GetType() == kWeaponCrossbow)) backstab = 0;
                TickBuildHit(this, nullptr, &pendingHit, backstab, location, calledShot);
                if (!TickResolveHit(defender, nullptr, &pendingHit, unit)) missed = true;
            }
            TickStartAction(this, nullptr, fresh, missed);
            return;
        }
    }
    if (stop) orderStopped = 1;
    ClearAction(&action);
    if (action2.kind == kOrderEmpty) reinterpret_cast<ControllerOriginals*>(this)->Idle();
}
EI_REPLACE_METHOD(0x0054D960, CUnitServer::ExecuteOrder);

// --- Closing in --------------------------------------------------------------------------------

namespace {
// Where the motion (+0x3C) is heading: half its +0x58 / +0x5C (as 1.0 * 0.5, the original's
// constant product).
double HalfScale() { return static_cast<double>(kOne) * kHalf; }
} // namespace

void CUnitServer::ApproachTarget() {
    int now = WorldTick(this);
    unit->unknown_43C = 1;
    CEngineObject* target = action.target;

    if (action.flag18 || !target) {
        // A point: walk there; fail when there is no way or the order cannot be done from there.
        Vector3 goal;
        std::memcpy(&goal, &action.point, sizeof(goal));
        if (state == kStateIdle) {
            if (!(DistanceSquaredXYUnrounded<double>(goal, motion.goal) > kNearPoint)) return;
        }
        if (TickPathTo(this, nullptr, &goal.x, kFarBits)) {
            double h = HalfScale();
            Vector3 heading;
            heading.x = static_cast<float>(h * motion.targetX);
            heading.y = static_cast<float>(h * motion.targetY);
            heading.z = 0.0f;
            double dx = static_cast<double>(heading.x) - goal.x;
            double dy = static_cast<double>(heading.y) - goal.y;
            if (!(LengthSquared2DUnrounded(dy, dx) > kNearPoint)) return;
            if (CtlCanReachTarget(this, nullptr, &action)) {
                Vector3 saved;
                std::memcpy(&saved, &unit->position.x, sizeof(saved));
                std::memcpy(&unit->position.x, &heading, sizeof(saved));
                bool ok = CtlCanPlanAction(this, nullptr, &action);
                std::memcpy(&unit->position.x, &saved, sizeof(saved));
                if (ok) return;
            }
        }
        ControllerShout(this, nullptr, action.kind == kOrderAttack ? kShoutTargetLost : kShoutCannotDo);
        action.CallReset();
        if (action2.kind == kOrderEmpty) reinterpret_cast<ControllerOriginals*>(this)->Idle();
        orderStopped = 1;
        return;
    }

    volatile float reach = static_cast<float>(CtlActionDistance(this, nullptr, &action));
    Vector3 aim;
    Slots(target)->AimPoint(&aim);
    if (!IsMoving(target)) {
        if (!LessOrUnordered(static_cast<double>(reach) * reach, DistanceSquaredXYUnrounded<double>(aim, unit->position)) &&
            CallCanActFrom(&action, &aim.x, now)) {
            // In reach of a standing target: wait.
            if (state != kStateTurning) StandIdle(this);
            return;
        }
    }
    if (IsMoving(target)) {
        int lead = nextAttackTime - now + kAttackLead;
        if (lead > 0) {
            // A moving target that comes into reach before the next attack: wait for it.
            Vector3 scratch;
            const float* p = &Slots(target)->PositionAt(&scratch, lead + now)->x;
            Vector3 offset;
            offset.x = p[0] - unit->position.x;
            offset.y = p[1] - unit->position.y;
            offset.z = p[2] - unit->position.z;
            double closing = Slots(unit)->MoveSpeed() * (static_cast<double>(kOne) * kHalf);
            if (closing > kZero) {
                double distance = LengthXYZ(offset);
                double ticks = (distance - reach) / closing;
                if (static_cast<double>(lead) > ticks) {
                    if (state != kStateTurning) StandIdle(this);
                    return;
                }
            }
        }
    }
    if (!(state == kStateChasing || state == kStateTurning) && interceptTime - now >= kAttackLead) return;

    // Chase: the target, or the unit the party leader is attacking it with.
    CEngineObject* chased = target;
    if (followsLeader) {
        CEngineObject* leader = followedLeader;
        auto* theirs = reinterpret_cast<CUnitServer*>(static_cast<CUnit*>(leader)->controller);
        if (theirs->action.target == target &&
            (theirs->action.kind == kOrderAttack || theirs->action.kind == kOrderCast || theirs->action.kind == kOrderCastAt) &&
            theirs->state != kStateCasting) {
            chased = leader;
        } else {
            followsLeader = 0;
            CEngineObject* old = followedLeader;
            followedLeader = nullptr;
            if (old) old->Release();
        }
    }
    Vector3 scratch;
    const float* where = &Slots(chased)->AimPoint(&scratch)->x;
    Vector3 offset;
    offset.x = where[0] - unit->position.x;
    offset.y = where[1] - unit->position.y;
    offset.z = where[2] - unit->position.z;
    volatile float distance = LengthZXY(offset);
    volatile float mySpeed = static_cast<float>(Slots(unit)->MoveSpeed());
    double closing;
    if (IsMoving(chased)) {
        // The chased unit's speed along the line between them adds to ours.
        auto* other = static_cast<CUnit*>(chased);
        volatile float vx = other->position.x - unit->position.x;
        volatile float vy = other->position.y - unit->position.y;
        double dz = static_cast<double>(other->position.z) - unit->position.z;
        volatile float vz = static_cast<float>(dz);
        double length = dz * vz;
        length = length + static_cast<double>(vy) * vy;
        length = length + static_cast<double>(vx) * vx;
        length = x87::fsqrt(length);
        if (length > kZero) {
            vx = static_cast<float>(vx / length);
            vy = static_cast<float>(vy / length);
            vz = static_cast<float>(vz / length);
        }
        Vector3 axis;
        other->orientation.AxisY(&axis);
        volatile float along = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(axis.x, axis.y, axis.z, vx, vy, vz));
        closing = Slots(other)->MoveSpeed() * along + mySpeed;
    } else {
        closing = mySpeed;
    }
    volatile float closingF = static_cast<float>(closing);
    float floor = std::bit_cast<float>(kMinClosingBits);
    float divisor = std::bit_cast<float>(LessOrUnordered(floor, closingF) ? std::bit_cast<std::uint32_t>(static_cast<float>(closingF))
                                                                           : kMinClosingBits);
    volatile float eta = static_cast<float>((static_cast<double>(distance) - reach) / divisor);
    int ticks = x87::fistp(eta);
    if (ticks > kChaseMaxTicks) ticks = kChaseMaxTicks;
    else if (ticks < kChaseMinTicks) ticks = kChaseMinTicks;
    int when = ticks + now;
    const float* future = &Slots(chased)->PositionAt(&scratch, when)->x;
    Vector3 goal;
    std::memcpy(&goal, future, sizeof(goal));

    if (state == kStateIdle) {
        double close = (static_cast<double>(distance) + kOne) * kPointOne;
        if (close * close > DistanceSquaredXYUnrounded<double>(goal, motion.goal)) {
            // Still heading close enough to it: only the patience (the path goal's z) changes.
            motion.goal.z = static_cast<float>(UnitScaledValue(unit, nullptr, distance) + kFive);
            return;
        }
    }
    volatile float patience = static_cast<float>(UnitScaledValue(unit, nullptr, distance) + kFive);
    CAIMap* aiMap = WorldServer()->AIMap();
    bool savedFlag = aiMap->refreshUnits;
    aiMap->refreshUnits = false;
    if (!(DistanceSquaredXYUnrounded<double>(unit->position, goal) > kPointOne)) return;   // (sic: the path flag stays cleared)

    SectorMapRemove(WorldServer()->Sectors(), nullptr, chased);
    volatile float far = static_cast<float>(static_cast<double>(goal.y) * kShort);
    double nearX = static_cast<double>(goal.x) * kShort;
    volatile float unitY = static_cast<float>(static_cast<double>(unit->position.y) * kLong);
    Vector3 point;
    point.x = static_cast<float>(static_cast<double>(unit->position.x) * kLong + nearX);
    point.y = static_cast<float>(static_cast<double>(unitY) + far);
    point.z = goal.z;
    if (!TickPathTo(this, nullptr, &point.x, std::bit_cast<std::uint32_t>(static_cast<float>(patience))))
        stoodBlocked = 1;
    SectorMapAdd(WorldServer()->Sectors(), nullptr, chased);

    double h = HalfScale();
    volatile float headX = static_cast<float>(h * motion.targetX);
    double fy = static_cast<double>(goal.y) - h * motion.targetY;
    double fx = static_cast<double>(goal.x) - headX;
    aiMap->refreshUnits = savedFlag;
    if (LessOrUnordered(static_cast<double>(reach) * reach, LengthSquared2DUnrounded(fx, fy))) {
        // The way leads elsewhere: stop, face along, and chase from here.
        AIPath* path = &motion;
        PathClear(path);
        int time = WorldTick(this);
        Vector3 axis;
        unit->orientation.AxisY(&axis);
        Vector3 back;
        back.x = -axis.x;
        back.y = -axis.y;
        back.z = -axis.z;
        if (AIPathStartOriginal(path, nullptr, &unit->position, &back, time)) UnitPathSpeeds(unit);
        CtlSetStanding(this, nullptr, 0);
        state = kStateChasing;
        UnitUpdateSizeClass(unit);
        if (effectObject) {
            WorldRemoveTimed(WorldServer(), nullptr, effectObject->id_);
            CEngineObject* old = effectObject;
            effectObject = nullptr;
            if (old) old->Release();
        }
        stoodBlocked = 1;
    }
    interceptTime = when;
}
EI_REPLACE_METHOD(0x0054D140, CUnitServer::ApproachTarget);
