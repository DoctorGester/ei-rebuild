// Combat, fifth part: the per-tick order machine of CUnitServer: taking over the queued order,
// carrying out the current one (closing in on the target, planning the moment of the action) and
// what is left when an order ends. See combat.h, combat_plan.h and docs/notes/combat.md ("Third
// round").
#include "combat.h"
#include "combat_plan.h"

#include "item.h"
#include "maths.h"
#include "random.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "originals_ai.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_units.h"

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

// Virtual slots.
constexpr int kSlotPositionAt = 0xD0 / 4;   // Vector3* (Vector3* out, int time)
constexpr int kSlotAimPoint = 0xD4 / 4;     // Vector3* (Vector3* out)
constexpr int kSlotIsMoving = 0xD8 / 4;     // int ()
constexpr int kSlotIsDead = 0xDC / 4;       // int ()
constexpr int kSlotSpeed = 0xEC / 4;        // ST0 ()
constexpr int kControllerWeaponSwitch = 0x64 / 4;   // CUnitServer slot 25 (weapon slot)

constexpr int kPartMoveModes = 6;           // MarkChanged part of the movement modes

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

bool LessOrUnordered(double a, double b) { return !(a >= b); }
bool ZeroOrUnordered(double a) { return !(a < 0.0 || a > 0.0); }
bool IsUnit(CEngineObject* object) { return object && object->GetClassId() == kClassUnit; }
bool IsCastKind(int kind) { return kind == kOrderCast || kind == kOrderCastAt; }
bool IsActionKind(int kind) { return kind == kOrderAttack || kind == kOrderCast || kind == kOrderCastAt || kind == kOrderSkill; }

IsaacRandom& Random() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }
std::uint8_t& PathFlag() { return *reinterpret_cast<std::uint8_t*>(0x007B00D2); }   // cleared around path searches
inline int& CurrentFrame() { return *reinterpret_cast<int*>(0x007AFFC4); }
void* const kSectorMap = reinterpret_cast<void*>(0x007B01F4);
void* const kWorldServer = reinterpret_cast<void*>(0x007AFF60);

CItemWeapon* ActiveWeapon(CUnit* unit) { return static_cast<CItemWeapon*>(unit->GetWeapon(unit->activeWeapon)); }
} // namespace

namespace {
// Not rewritten (forwarders), as __fastcall with the unused EDX for __thiscall:
// 00455950 the object's world; 0066D6E0 party messages; 00552780 the unit shouts.
__declspec(naked) void __fastcall TickMessage(void*, void*, CUnit*, int) EI_ORIGINAL(0x0066D6E0)
__declspec(naked) void __fastcall ControllerShout(CUnitServer*, void*, int) EI_ORIGINAL(0x00552780)
// CUnitServer helpers: 0054A150 follow the path (true while moving), 00550F30 the idle animation
// (+0x960), 00546C40 play it, 00547280 / 00547090 / 005474B0 / 0054EB30 / 005467B0 movement and
// motion updates, 00548700 plan a path to a point, 005468D0 take nearby units off the sector map
// into a list, 005494D0 start the planned action, 00548DE0 / 00548FA0 play an animation list
// (taken by value), 00549690 the end of an order.
__declspec(naked) bool __fastcall TickFollowPath(CUnitServer*) EI_ORIGINAL(0x0054A150)
__declspec(naked) void __fastcall TickPlayIdle(CUnitServer*, void*, AnimationSteps*) EI_ORIGINAL(0x00546C40)
__declspec(naked) void __fastcall TickMotion5474B0(CUnitServer*) EI_ORIGINAL(0x005474B0)
__declspec(naked) void __fastcall TickStartAction(CUnitServer*, void*, int, int) EI_ORIGINAL(0x005494D0)
__declspec(naked) void __fastcall TickPlaySteps(CUnitServer*, void*, AnimationSteps) EI_ORIGINAL(0x00548DE0)
__declspec(naked) void __fastcall TickPlayEmote(CUnitServer*, void*, AnimationSteps) EI_ORIGINAL(0x00548FA0)
__declspec(naked) bool __fastcall TickTalkTargetBusy(CUnitServer*, void*, CEngineObject*) EI_ORIGINAL(0x0054C760)
__declspec(naked) bool __fastcall TickCheckOrder(CUnitServer*, void*, const UnitAction*) EI_ORIGINAL(0x0054C8B0)
// Containers: 00544E70 the animation queue (+0x24) holds a list; 00544D50 an emote's steps;
// 00545110 the plan takes a list (by value), its start and duration; 0054B260 / 00557E70 copy /
// destroy a list; 00557B80 SetSize; 00544CE0 AddStep; 0052CA10 a CPtrList's RemoveAll.
__declspec(naked) bool __fastcall QueueHolds(void*, void*, AnimationSteps*) EI_ORIGINAL(0x00544E70)
__declspec(naked) void __fastcall StepsEmote(AnimationSteps*, void*, int) EI_ORIGINAL(0x00544D50)
__declspec(naked) void __fastcall PlanTake(OrderPlan*, void*, AnimationSteps, int, int) EI_ORIGINAL(0x00545110)
__declspec(naked) AnimationSteps* __fastcall StepsCopy(AnimationSteps*, void*, const AnimationSteps*) EI_ORIGINAL(0x0054B260)
__declspec(naked) void __fastcall StepsDestroy(AnimationSteps*) EI_ORIGINAL(0x00557E70)
__declspec(naked) void __fastcall StepsAddStepTick(AnimationSteps*, void*, int, int) EI_ORIGINAL(0x00544CE0)
__declspec(naked) void __fastcall PtrListRemoveAll(void*) EI_ORIGINAL(0x0052CA10)
// Motion (+0x3C): 005B1DE0 turn to a point, 005B23A0 stop, 005C7B00 place along a direction.
__declspec(naked) void __fastcall MotionTurn(void*, void*, int, CUnit*, const float*, int) EI_ORIGINAL(0x005B1DE0)
__declspec(naked) bool __fastcall MotionPlace(void*, void*, const float*, const Vector3*, int) EI_ORIGINAL(0x005C7B00)
// CUnit: 0052E9D0 path speeds, 00530570 size class, 00530A60 IsBehind (unit_hits.cpp),
// 0052FBC0 ScaledValue (unit.cpp; ST0).
__declspec(naked) BOOL __fastcall UnitIsBehind(CUnit*, void*, CEngineObject*) EI_ORIGINAL(0x00530A60)
__declspec(naked) double __fastcall UnitScaledValue(CUnit*, void*, float) EI_ORIGINAL(0x0052FBC0)
// The AI map's cell flag 0x80 under a point (005BBC70), the sector map (CSectorMap::Remove /
// Add, sector_map.cpp), the world's timed objects (0058E5D0, world_server.cpp), the brain's
// leader (005D5E90, unit_ai.cpp), the memory's "is an enemy" (005CB7F0), the party's sight of a
// unit (0066E110).
__declspec(naked) std::uint8_t __fastcall MapCellFlag80(void*, void*, const float*) EI_ORIGINAL(0x005BBC70)
__declspec(naked) void __fastcall WorldRemoveTimed(void*, void*, int) EI_ORIGINAL(0x0058E5D0)
__declspec(naked) void* __fastcall BrainLeader(void*) EI_ORIGINAL(0x005D5E90)
__declspec(naked) int __fastcall MemoryIsEnemy(void*, void*, CUnit*, CEngineObject*) EI_ORIGINAL(0x005CB7F0)
__declspec(naked) bool __fastcall PartySees(void*, void*, CEngineObject*) EI_ORIGINAL(0x0066E110)
// Rewritten elsewhere (combat*.cpp), through their game addresses: ActionDistance, CanReachTarget,
// CanCast, BuildAttackHit, ResolveHit, AnimationDuration, BuildOrderSteps.
__declspec(naked) bool __fastcall TickCanCast(CUnitServer*, void*, const UnitAction*) EI_ORIGINAL(0x0054BF80)
__declspec(naked) BOOL __fastcall TickBuildHit(CUnitServer*, void*, DamageInfo*, int, int, char) EI_ORIGINAL(0x0054F630)
__declspec(naked) BOOL __fastcall TickResolveHit(CUnitServer*, void*, DamageInfo*, CUnit*) EI_ORIGINAL(0x00553DF0)
__declspec(naked) int __fastcall TickAnimationDuration(CUnitServer*, void*, std::uint32_t) EI_ORIGINAL(0x00551A40)
__declspec(naked) void __fastcall TickOrderSteps(CUnitServer*, void*, AnimationSteps*, int*, int, std::uint32_t)
    EI_ORIGINAL(0x00550100)
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
    if (unit->player) TickMessage(*reinterpret_cast<void**>(unit->player + 0x20), nullptr, unit, message);
}

// 00545F40 (UnitAction::Clear) followed by the +0x1C / +0x20 reset, as callers inline it.
void ClearAction(UnitAction* order) {
    ActionClear(order);
    order->unknown_1C = 0;
    order->unknown_20 = 0;
}

// A CPtrList on the stack (vtable 0073D454), as 005468D0 fills it.
struct UnitPtrList {
    void* vtable;
    struct Node {
        Node* next;
        Node* prev;
        CEngineObject* data;
    }* head;
    Node* tail;
    int count;
    Node* free;
    void* blocks;
    int blockSize;
};
static_assert(sizeof(UnitPtrList) == 0x1C);
void* const kPtrListVtable = reinterpret_cast<void*>(0x0073D454);
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
    std::memcpy(point, from->point, sizeof(point));
    flag18 = from->flag18;
    unknown_1C = from->unknown_1C;
    unknown_1C = from->unknown_1C;   // copied twice by the original
    unknown_20 = from->unknown_20;
    return this;
}
EI_REPLACE_METHOD(0x0054B320, UnitAction::Assign);

void UnitAction::Reset() {
    kind = kOrderEmpty;
    if (target) target->Release();
    target = nullptr;
    flag18 = 0;
    unknown_1C = 0;
    unknown_20 = 0;
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
    if (action.unknown_20 && moveMode.mode == kModeRun) moveMode2.wasMode3 |= 2;
    if (!unit->player && moveMode2.flag4) moveMode2.wasMode3 |= 1;
}
EI_REPLACE_METHOD(0x0054A620, CUnitServer::SyncMoveModes);

namespace {
// The melee reach test of 0054C8B0 as 0054A6E0 inlines it: false when the target (a living unit
// standing on its own) is too high to reach, else whether the action can be planned (0054C6B0).
bool CanStillDo(CUnitServer* self, UnitAction* order) {
    if (order->kind == kOrderAttack || order->kind == kOrderSkill) {
        CEngineObject* target = order->target;
        if (IsUnit(target) && ei::VCall<int>(target, kSlotIsDead) == 0) {
            auto* other = static_cast<CUnit*>(target);
            if (other->sizeClass == 0 && !(order->kind == kOrderAttack && self->HasRangedWeapon())) {
                double height = static_cast<double>(At<float>(other->GetPrototype(), 0x24)) + other->positionZ;
                if (!LessOrUnordered(height, kHeightLimit)) return false;
            }
        }
    }
    return self->CallCanPlanAction(order);
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
    std::memcpy(action.point, action2.point, sizeof(action.point));
    action.flag18 = action2.flag18;
    action.unknown_1C = action2.unknown_1C;
    action.unknown_1C = action2.unknown_1C;
    action.unknown_20 = action2.unknown_20;
    action2.kind = kOrderEmpty;
    if (action2.target) action2.target->Release();
    action2.target = nullptr;
    action2.flag18 = 0;
    action2.unknown_1C = 0;
    action2.unknown_20 = 0;
    if (action.unknown_20 && moveMode.mode != kModeRun) {
        if (!(IsActionKind(action.kind) && CanStillDo(this, &action)) && unit->moveMode2 != kModeRun) {
            unit->moveMode2 = kModeRun;
            unit->MarkChanged(kPartMoveModes);
        }
    }
    CallSyncMoveModes();
    At<int>(this, 0x93C) = CurrentFrame();
}
EI_REPLACE_METHOD(0x0054A6E0, CUnitServer::TakeQueuedOrder);

// --- Taking over the queued order ----------------------------------------------------------------

namespace {
// The common start of taking over the queued order: run (mode 2) unless the order can be carried
// out at once, then the movement records and the frame stamp.
void LeaveMoveMode(CUnitServer* self) {
    UnitAction* order = &self->action;
    if (order->unknown_20 && self->moveMode.mode != kModeRun) {
        int kind = order->kind;
        if (!(IsActionKind(kind) && TickCheckOrder(self, nullptr, order))) self->CallSetUnitMoveMode2(kModeRun);
    }
    self->CallSyncMoveModes();
    At<int>(self, 0x93C) = CurrentFrame();
}

// Stand: the idle animation (restarted at a random phase when nothing plays) and the motion
// updates. Not while turning (state 2).
void StandIdle(CUnitServer* self) {
    AnimationSteps* idle = CtlIdleAnimation(self);
    if (!QueueHolds(&At<char>(self, 0x24), nullptr, idle)) {
        if (At<int>(self, 0x2C) == 0 && At<int>(self, 0x38) == -1) {
            int now = At<int>(ObjectWorld(self), 0x64);
            idle->extra = now - static_cast<int>(Random().Next() % 0x33);
        }
        TickPlayIdle(self, nullptr, idle);
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
    list->vtable = kPtrListVtable;
    list->blockSize = 10;
}

void DestroyPtrList(UnitPtrList* list) {
    list->vtable = kPtrListVtable;
    PtrListRemoveAll(list);
}
} // namespace

bool CUnitServer::StartMove() {
    void* world = ManagerContext();
    UnitPtrList lifted;
    InitPtrList(&lifted);
    At<std::uint8_t>(this, 0x18C) = 0;
    TakeOver(this);
    LeaveMoveMode(this);
    if (TickFollowPath(this)) {
        DestroyPtrList(&lifted);
        return true;
    }
    // Already there: the order is done and the next one starts.
    Vector3 offset;
    offset.x = unit->positionX - action.point[0];
    offset.y = unit->positionY - action.point[1];
    offset.z = unit->positionZ - action.point[2];
    double lengthSq = static_cast<double>(offset.z) * offset.z + static_cast<double>(offset.y) * offset.y;
    lengthSq = lengthSq + static_cast<double>(offset.x) * offset.x;
    if (LessOrUnordered(lengthSq, kArrived)) {
        action.CallReset();
        if (action2.kind == kOrderEmpty) CallIdle();
        bool result = CallStartQueuedOrder();
        DestroyPtrList(&lifted);
        return result;
    }
    // Plan the way with the units near the goal lifted off the sector map.
    CtlLiftNearby(this, nullptr, &lifted, action.point, action.source != 0);
    void* map = static_cast<char*>(ManagerContext()) + 0x170;
    At<std::uint32_t>(unit, 0x43C) = MapCellFlag80(map, nullptr, &unit->positionX);
    bool found = TickPathTo(this, nullptr, action.point, std::bit_cast<std::uint32_t>(action.point[2]));
    for (UnitPtrList::Node* node = lifted.head; node;) {
        UnitPtrList::Node* current = node;
        node = node->next;
        SectorMapAdd(AsSectorMap(static_cast<char*>(world) + 0x294), nullptr, current->data);
    }
    bool arrived = false;
    if (action.unknown_1C) {
        action.unknown_1C = 0;
        bool close = false;
        if (!found) {
            double dy = static_cast<double>(action.point[1]) - unit->positionY;
            double dx = static_cast<double>(action.point[0]) - unit->positionX;
            close = LessOrUnordered(dx * dx + dy * dy, kLong);
        }
        if (close) arrived = true;
        else CtlReportPath(this, nullptr, &At<char>(this, 0x3C), found);
    }
    if (!arrived && found) {
        DestroyPtrList(&lifted);
        return true;
    }
    At<std::uint8_t>(this, 0x18C) = 1;
    action.CallReset();
    if (action2.kind == kOrderEmpty) CallIdle();
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
        int time = action.unknown_1C;
        void* world = ManagerContext();
        MotionTurn(&At<char>(this, 0x3C), nullptr, At<int>(world, 0x64), unit, action.point, time);
        UnitPathSpeeds(unit);
        TickMotion5474B0(this);
        CtlSetState(this, nullptr, 1);
        return true;
    }

    case kOrderAttack:
    case kOrderCast:
    case kOrderCastAt:
    case kOrderSkill:
        At<std::uint8_t>(this, 0x18C) = 0;
        TakeOver(this);
        LeaveMoveMode(this);
        At<std::uint8_t>(this, 0x145) = 0;
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
        std::uint8_t animation = static_cast<std::uint8_t>(action.unknown_1C);
        int duration = TickAnimationDuration(this, nullptr, animation);
        StepsAddStepTick(&steps, nullptr, animation, duration);
        AnimationSteps copy;
        StepsCopy(&copy, nullptr, &steps);
        TickPlaySteps(this, nullptr, copy);
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
        StepsEmote(&steps, nullptr, static_cast<std::uint8_t>(action.unknown_1C));
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
        TickOrderSteps(this, nullptr, &steps, &duration, kOrderSkill, 0);
        int now = At<int>(ObjectWorld(this), 0x64);
        AnimationSteps copy;
        StepsCopy(&copy, nullptr, &steps);
        PlanTake(reinterpret_cast<OrderPlan*>(&At<char>(this, 0xC4)), nullptr, copy, now, duration);
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
            if (action2.kind == kOrderEmpty) CallIdle();
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
        At<int>(this, 0x938) = 0;
        return;
    }
    CallSyncMoveModes();
    if (TickFollowPath(this)) return;
    switch (action.kind) {
    case kOrderMove:
    case kOrderTurn:
        if (state != kStateChasing) return;
        if (At<std::uint8_t>(this, 0x145)) {
            At<std::uint8_t>(this, 0x18C) = 1;
            if (!BrainLeader(brain) && unit->player) TellParty(unit, kMessageCannotDo);
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
    if (player && IsUnit(target) && !MemoryIsEnemy(&brain[0xD0], nullptr, unit, target))
        seen = PartySees(*reinterpret_cast<void**>(player + 0x20), nullptr, target);

    bool cast = IsCastKind(action.kind);
    bool carryOn = false;
    if (!(cast && (unit->flags_ & kFlagNoCasting))) {
        if (!action.target) {
            if (cast) {
                void* spell = SpellOfSource(action.source);
                carryOn = spell && (At<std::uint32_t>(spell, 0x3C) & kSpellNoTarget);
            }
        } else if (ei::VCall<int>(action.target, kSlotIsDead) == 0) {
            carryOn = true;
        } else if (action.kind == kOrderSkill && (action.source == kSkillOnTarget || action.source == 0)) {
            carryOn = true;   // talk to or study the dead
        }
    }
    bool failed = false;
    if (carryOn) {
        if (At<std::uint8_t>(this, 0x145)) {
            failed = true;
        } else if (seen) {
            CallExecuteOrder();
            return;
        }
    } else if (At<std::uint8_t>(this, 0x145)) {
        failed = true;
    }
    if (failed) {
        At<std::uint8_t>(this, 0x18C) = 1;
        TellParty(unit, action.kind == kOrderAttack ? kMessageTargetLost : kMessageCannotDo);
    }
    if (!seen) {
        At<std::uint8_t>(this, 0x18C) = 1;
        TellParty(unit, kMessageUnseen);
    }
    action.kind = kOrderEmpty;
    if (action.target) action.target->Release();
    action.target = nullptr;
    action.flag18 = 0;
    action.unknown_1C = 0;
    action.unknown_20 = 0;
    if (action2.kind == kOrderEmpty && action.kind != kOrderNone) DropQueuedOrder();
    TickOrderDone(this, nullptr, 0);
}
EI_REPLACE_METHOD(0x0054DD70, CUnitServer::ContinueAction);

// --- Carrying out the current order ----------------------------------------------------------------

namespace {
// The unit's attack interval: the weapon prototype's +0x6C (or stats +0x8C truncated) scaled by
// 15 / stats +0x30 (unscaled when that is 0 or NaN), rounded.
int AttackInterval(CUnitServer* self, CItemWeapon* weapon) {
    volatile float factor = At<float>(self, 0x210);
    if (!ZeroOrUnordered(factor)) factor = kFifteen / factor;
    int base;
    if (weapon) {
        base = At<int>(weapon->GetPrototype(), 0x6C);
    } else {
        double value = At<float>(self, 0x26C);
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
    if (!IsUnit(target) || ei::VCall<int>(target, kSlotIsDead) != 0) return false;
    auto* other = static_cast<CUnit*>(target);
    if (other->sizeClass != 0) return false;
    if (order->kind == kOrderAttack && self->HasRangedWeapon()) return false;
    double height = static_cast<double>(At<float>(other->GetPrototype(), 0x24)) + other->positionZ;
    return !LessOrUnordered(height, kHeightLimit);
}
} // namespace

void CUnitServer::ExecuteOrder() {
    int now = At<int>(ObjectWorld(this), 0x64);
    At<std::uint8_t>(this, 0x18E) = 0;
    if (At<int>(this, 0x184) > now && action.kind == kOrderAttack) {
        CallApproachTarget();
        return;
    }
    if (unit->unknown_270 != static_cast<int>(unit->activeWeapon))
        ei::VCall<void>(unit->controller, kControllerWeaponSwitch, unit->unknown_270);
    auto* plan = reinterpret_cast<OrderPlan*>(&At<char>(this, 0xC4));
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
            At<std::uint8_t>(this, 0x18E) = 1;
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
        int& nextAttack = At<int>(this, 0x184);
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
                int location = plan->unknown_2C;
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
    if (stop) At<std::uint8_t>(this, 0x18C) = 1;
    ClearAction(&action);
    if (action2.kind == kOrderEmpty) CallIdle();
}
EI_REPLACE_METHOD(0x0054D960, CUnitServer::ExecuteOrder);

// --- Closing in --------------------------------------------------------------------------------

namespace {
// Where the motion (+0x3C) is heading: half its +0x58 / +0x5C (as 1.0 * 0.5, the original's
// constant product).
double HalfScale() { return static_cast<double>(kOne) * kHalf; }
} // namespace

void CUnitServer::ApproachTarget() {
    int now = At<int>(ObjectWorld(this), 0x64);
    At<int>(unit, 0x43C) = 1;
    CEngineObject* target = action.target;

    if (action.flag18 || !target) {
        // A point: walk there; fail when there is no way or the order cannot be done from there.
        float goal[3];
        std::memcpy(goal, action.point, sizeof(goal));
        if (state == kStateIdle) {
            double dy = static_cast<double>(goal[1]) - At<float>(this, 0xA0);
            double dx = static_cast<double>(goal[0]) - At<float>(this, 0x9C);
            if (!(dx * dx + dy * dy > kNearPoint)) return;
        }
        if (TickPathTo(this, nullptr, goal, kFarBits)) {
            double h = HalfScale();
            Vector3 heading;
            heading.x = static_cast<float>(h * At<float>(this, 0x94));
            heading.y = static_cast<float>(h * At<float>(this, 0x98));
            heading.z = 0.0f;
            double dx = static_cast<double>(heading.x) - goal[0];
            double dy = static_cast<double>(heading.y) - goal[1];
            if (!(dy * dy + dx * dx > kNearPoint)) return;
            if (CtlCanReachTarget(this, nullptr, &action)) {
                float saved[3];
                std::memcpy(saved, &unit->positionX, sizeof(saved));
                std::memcpy(&unit->positionX, &heading, sizeof(saved));
                bool ok = CallCanPlanAction(&action);
                std::memcpy(&unit->positionX, saved, sizeof(saved));
                if (ok) return;
            }
        }
        ControllerShout(this, nullptr, action.kind == kOrderAttack ? kShoutTargetLost : kShoutCannotDo);
        action.CallReset();
        if (action2.kind == kOrderEmpty) CallIdle();
        At<std::uint8_t>(this, 0x18C) = 1;
        return;
    }

    volatile float reach = static_cast<float>(CtlActionDistance(this, nullptr, &action));
    Vector3 aim;
    ei::VCall<float*>(target, kSlotAimPoint, &aim);
    if (!ei::VCall<int>(target, kSlotIsMoving)) {
        double dy = static_cast<double>(aim.y) - unit->positionY;
        double dx = static_cast<double>(aim.x) - unit->positionX;
        if (!LessOrUnordered(static_cast<double>(reach) * reach, dx * dx + dy * dy) &&
            CallCanActFrom(&action, &aim.x, now)) {
            // In reach of a standing target: wait.
            if (state != kStateTurning) StandIdle(this);
            return;
        }
    }
    if (ei::VCall<int>(target, kSlotIsMoving)) {
        int lead = At<int>(this, 0x184) - now + kAttackLead;
        if (lead > 0) {
            // A moving target that comes into reach before the next attack: wait for it.
            Vector3 scratch;
            const float* p = ei::VCall<float*>(target, kSlotPositionAt, &scratch, lead + now);
            Vector3 offset;
            offset.x = p[0] - unit->positionX;
            offset.y = p[1] - unit->positionY;
            offset.z = p[2] - unit->positionZ;
            double closing = ei::VCall<double>(unit, kSlotSpeed) * (static_cast<double>(kOne) * kHalf);
            if (closing > kZero) {
                double distance = x87::fsqrt(static_cast<double>(offset.x) * offset.x +
                                             static_cast<double>(offset.y) * offset.y +
                                             static_cast<double>(offset.z) * offset.z);
                double ticks = (distance - reach) / closing;
                if (static_cast<double>(lead) > ticks) {
                    if (state != kStateTurning) StandIdle(this);
                    return;
                }
            }
        }
    }
    if (!(state == kStateChasing || state == kStateTurning) && At<int>(this, 0x188) - now >= kAttackLead) return;

    // Chase: the target, or the unit the party leader is attacking it with.
    CEngineObject* chased = target;
    if (At<std::uint8_t>(this, 0x1D8)) {
        CEngineObject* leader = At<CEngineObject*>(this, 0x1DC);
        auto* theirs = reinterpret_cast<CUnitServer*>(static_cast<CUnit*>(leader)->controller);
        if (theirs->action.target == target &&
            (theirs->action.kind == kOrderAttack || theirs->action.kind == kOrderCast || theirs->action.kind == kOrderCastAt) &&
            theirs->state != kStateCasting) {
            chased = leader;
        } else {
            At<std::uint8_t>(this, 0x1D8) = 0;
            CEngineObject*& slot = At<CEngineObject*>(this, 0x1DC);
            CEngineObject* old = slot;
            slot = nullptr;
            if (old) old->Release();
        }
    }
    Vector3 scratch;
    const float* where = ei::VCall<float*>(chased, kSlotAimPoint, &scratch);
    Vector3 offset;
    offset.x = where[0] - unit->positionX;
    offset.y = where[1] - unit->positionY;
    offset.z = where[2] - unit->positionZ;
    double sum = static_cast<double>(offset.x) * offset.x + static_cast<double>(offset.z) * offset.z;
    volatile float distance = static_cast<float>(x87::fsqrt(sum + static_cast<double>(offset.y) * offset.y));
    volatile float mySpeed = static_cast<float>(ei::VCall<double>(unit, kSlotSpeed));
    double closing;
    if (ei::VCall<int>(chased, kSlotIsMoving)) {
        // The chased unit's speed along the line between them adds to ours.
        auto* other = static_cast<CUnit*>(chased);
        volatile float vx = other->positionX - unit->positionX;
        volatile float vy = other->positionY - unit->positionY;
        double dz = static_cast<double>(other->positionZ) - unit->positionZ;
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
        reinterpret_cast<const Quaternion*>(&At<float>(other, 0x34))->AxisY(&axis);
        double dot = static_cast<double>(axis.z) * vz + static_cast<double>(axis.y) * vy;
        volatile float along = static_cast<float>(dot + static_cast<double>(axis.x) * vx);
        closing = ei::VCall<double>(other, kSlotSpeed) * along + mySpeed;
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
    const float* future = ei::VCall<float*>(chased, kSlotPositionAt, &scratch, when);
    float goal[3];
    std::memcpy(goal, future, sizeof(goal));

    if (state == kStateIdle) {
        double close = (static_cast<double>(distance) + kOne) * kPointOne;
        double dy = static_cast<double>(goal[1]) - At<float>(this, 0xA0);
        double dx = static_cast<double>(goal[0]) - At<float>(this, 0x9C);
        if (close * close > dx * dx + dy * dy) {
            // Still heading close enough to it: only the patience changes.
            At<float>(this, 0xA4) = static_cast<float>(UnitScaledValue(unit, nullptr, distance) + kFive);
            return;
        }
    }
    volatile float patience = static_cast<float>(UnitScaledValue(unit, nullptr, distance) + kFive);
    std::uint8_t savedFlag = PathFlag();
    PathFlag() = 0;
    double ay = static_cast<double>(unit->positionY) - goal[1];
    double ax = static_cast<double>(unit->positionX) - goal[0];
    if (!(ax * ax + ay * ay > kPointOne)) return;   // (sic: the path flag stays cleared)

    SectorMapRemove(AsSectorMap(kSectorMap), nullptr, chased);
    volatile float far = static_cast<float>(static_cast<double>(goal[1]) * kShort);
    double nearX = static_cast<double>(goal[0]) * kShort;
    volatile float unitY = static_cast<float>(static_cast<double>(unit->positionY) * kLong);
    float point[3];
    point[0] = static_cast<float>(static_cast<double>(unit->positionX) * kLong + nearX);
    point[1] = static_cast<float>(static_cast<double>(unitY) + far);
    point[2] = goal[2];
    if (!TickPathTo(this, nullptr, point, std::bit_cast<std::uint32_t>(static_cast<float>(patience))))
        At<std::uint8_t>(this, 0x145) = 1;
    SectorMapAdd(AsSectorMap(kSectorMap), nullptr, chased);

    double h = HalfScale();
    volatile float headX = static_cast<float>(h * At<float>(this, 0x94));
    double fy = static_cast<double>(goal[1]) - h * At<float>(this, 0x98);
    double fx = static_cast<double>(goal[0]) - headX;
    PathFlag() = savedFlag;
    if (LessOrUnordered(static_cast<double>(reach) * reach, fx * fx + fy * fy)) {
        // The way leads elsewhere: stop, face along, and chase from here.
        void* motion = &At<char>(this, 0x3C);
        PathClear(motion);
        int time = At<int>(ObjectWorld(this), 0x64);
        Vector3 axis;
        reinterpret_cast<const Quaternion*>(&At<float>(unit, 0x34))->AxisY(&axis);
        Vector3 back;
        back.x = -axis.x;
        back.y = -axis.y;
        back.z = -axis.z;
        if (MotionPlace(motion, nullptr, &unit->positionX, &back, time)) UnitPathSpeeds(unit);
        CtlSetStanding(this, nullptr, 0);
        state = kStateChasing;
        UnitUpdateSizeClass(unit);
        if (effectObject) {
            WorldRemoveTimed(kWorldServer, nullptr, effectObject->id_);
            CEngineObject* old = effectObject;
            effectObject = nullptr;
            if (old) old->Release();
        }
        At<std::uint8_t>(this, 0x145) = 1;
    }
    At<int>(this, 0x188) = when;
}
EI_REPLACE_METHOD(0x0054D140, CUnitServer::ApproachTarget);
