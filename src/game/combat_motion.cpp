// Combat, seventh part: the controller's movement modes, idle and movement animations, paths to a
// point, the start of a planned action and the controller's reset. See combat.h, combat_plan.h
// and docs/notes/combat.md ("Fourth round").
//
// Animation lists cached on the controller: +0x944 the movement animation (key +0x940), +0x960
// the idle animation (key +0x95C; its start time +0x974).
#include "ai_map.h"
#include "ai_motion.h"
#include "combat.h"
#include "originals_combat.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "item.h"
#include "map_object_figures.h"
#include "player_messages.h"
#include "random.h"
#include "unit_ai.h"
#include "world.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_ai.h"
#include "originals_core.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_units.h"
#include "originals_world.h"
#include "originals_gameplay.h"
#include "originals_script.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

constexpr int kStateIdle = 0;
constexpr int kStateTurning = 2;
constexpr int kStateAnimation = 3;   // an animation order plays (00548DE0)
constexpr int kStateEmote = 4;       // an emote plays (00548FA0)
constexpr int kStateActing = 5;
constexpr int kStateChasing = 6;
constexpr int kOrderMove = 1;
constexpr int kOrderAttack = 3;
constexpr int kOrderNone = 9;
constexpr int kModeRun = 2;
constexpr std::uint32_t kKeyIdle = 0x80580000;
constexpr std::uint32_t kKeyMoveOnce = 0x40000000;
constexpr std::uint32_t kKeyMoveLoop = 0x80000000;
constexpr std::uint8_t kEmoteStand = 0xFE;   // the idle "emote" of a walking hero
constexpr float kNoRange = std::bit_cast<float>(0x7E967699u);   // 1e38
constexpr std::uint32_t kFarBits = 0x49742400u;                 // 1e6
constexpr int kMessageModeBlocked = 0x0D;
constexpr int kMessageCannotDo = 0x0E;
constexpr int kMessagePartial = 0x01;

// --- Forwarders (thiscall as __fastcall with the unused EDX) --------------------------------------

// CUnit: 0050E9F0 MarkChanged, 0052EA90 the animation bytes, 00530570 the size class, 005301B0
// the animation entry for a key (combat_steps.cpp), 0052E9D0 the path speeds.
// 0040F560 Quaternion::AxisY, 00402200 NegateVector (core_maths.cpp).
// UnitStats (unit_vitals.cpp / unit_stats_pack.cpp): 005241C0 the stamina state for a movement
// (1: running), 005233E0 Reset.
__declspec(naked) int __fastcall StatsMovementState(UnitStats*, void*, int) EI_ORIGINAL(0x005241C0)
// The AI map (world + 0x170, ai_map.cpp / ai_movement.cpp / ai_path.cpp): 005BA6E0
// IsUnitCellPassable, 005BA830 CanLeaveCell, 005BA5A0 StandsOnCheapCell, 005BB350 FindPath.
__declspec(naked) bool __fastcall MapCellPassable(void*, void*, CUnit*) EI_ORIGINAL(0x005BA6E0)
__declspec(naked) bool __fastcall MapCheapCell(void*, void*, CUnit*) EI_ORIGINAL(0x005BA5A0)
// The sector map (world + 0x294 = 007B01F4): 005C1010 Remove, 005C0DF0 Add (returns AL),
// 005C0BD0 FindUnitsInRadius.
// 004B3330 ScriptGroup::Contains (script_group.cpp).
// The motion (+0x3C, an AIPath; ai_motion.cpp / ai_path_stand.cpp): 005B23A0 Clear, 005C7B00 Start,
// 005B1F10 follow a target, 005B2050 turn to a direction, 005B22C0 the path's end point.
__declspec(naked) void __fastcall PathFollow(void*, void*, int /*now*/, CUnit*, int /*targetId*/, int /*until*/,
                                             int /*start*/) EI_ORIGINAL(0x005B1F10)
__declspec(naked) void __fastcall PathTurn(void*, void*, int /*now*/, CUnit*, const float* /*direction*/,
                                           int /*until*/, int /*start*/) EI_ORIGINAL(0x005B2050)
// The step list of a motion order (a CList of path cells, vtable 0073EDB0): 00556D10 constructor
// (block size), 00556D40 RemoveAll.
void* __fastcall CellListConstruct(void* a0, void*, int a2) { return CellListConstructOriginal(a0, nullptr, a2); }
void __fastcall CellListRemoveAll(void* a0) { CellListRemoveAllOriginal(a0); }
// The world server's timed objects (world_server.cpp).
// The AI: 005D5E90 the brain's leader (BrainLeaderOrFollowedOriginal), 005D4F70 the brain's reset, 005AEA40 the
// alarm's reset.
// 0066D490: the player's message about a path to `point` (kind, flag, distance limit); the motion
// order (the path, serialised for kinds other than 3).
void __fastcall PlayerPathMessage(void* a0, void*, CUnit* a2, const void* a3, const float* a4, int a5, int a6, std::uint32_t a7) {
    PlayerPathMessageOriginal(a0, nullptr, a2, a3, a4, a5, a6, a7);
}
// The controller's functions rewritten elsewhere, through their game addresses (combat_update.cpp,
// combat_steps.cpp, combat_tick.cpp).
__declspec(naked) std::uint32_t __fastcall CtlWeaponTypeMask(CUnitServer*) EI_ORIGINAL(0x00550050)
__declspec(naked) std::uint32_t __fastcall CtlMoveModeMask(CUnitServer*, void*, const MoveModeState*) EI_ORIGINAL(0x005500A0)
// And the functions of this file.
__declspec(naked) void __fastcall CtlBuildIdle(CUnitServer*, void*, AnimationSteps*) EI_ORIGINAL(0x00550D90)
// Animation lists (MFC CArray<AnimationStep> instantiations, library code): 00556950 / 005483C0
// Copy, 00556870 copy constructor, 00557B60 constructor, 00557E70 destructor, 00557B80 SetSize,
// 00558950 SetAtGrow, 00557CC0 Add, 00544D50 add an emote (a pause step's +1 byte); 00550A60 a
// blank step; 00544FD0 OrderPlan's reset.
AnimationSteps* __fastcall StepsCopyConstruct(AnimationSteps* a0, void*, const AnimationSteps* a2) { return (AnimationSteps*)StepsCopyConstructOriginal((void*)a0, nullptr, (const void*)a2); }
AnimationSteps* __fastcall StepsConstruct(AnimationSteps* a0) { return (AnimationSteps*)StepsConstructOriginal((void*)a0); }
inline void __fastcall StepsDestroy(AnimationSteps* s) { AnimationStepsDestroyOriginal(s); }
void __fastcall StepsEmote(AnimationSteps* a0, void*, std::uint32_t a2) { StepsEmoteOriginal((void*)a0, nullptr, (int)a2); }
__declspec(naked) AnimationStep* __fastcall StepBlank(AnimationStep*) EI_ORIGINAL(0x00550A60)
// MFC: CPlex::FreeDataChain (006FE90F); the CList<CUnit*> node allocation (0052CEF0, NewNode(prev,
// next)) and release (005588A0, FreeNode) of the lists the controller fills.
inline void __fastcall PlexFreeDataChain(void* p) { PlexFreeDataChainOriginal(p); }
void* __fastcall ListNewNode(void* a0, void*, void* a2, void* a3) { return PtrListNewNode52CEF0Original(a0, nullptr, a2, a3); }
void __fastcall ListFreeNode(void* a0, void*, void* a2) { UnitListFreeNodeOriginal(a0, nullptr, a2); }

// The world server (007AFF60), its AI map (+0x170 = 007B00D0) and sector map (+0x294 = 007B01F4).
CAIMap* ServerAIMap() { return WorldServer()->AIMap(); }
CSectorMap* ServerSectors() { return WorldServer()->Sectors(); }
float HalfCell() { return *reinterpret_cast<const float*>(0x0073EEE4) * *reinterpret_cast<const float*>(0x0073B86C); }
float HeroWalkLimit() { return *reinterpret_cast<const float*>(0x007C5448); }
float HeroRunLimit() { return *reinterpret_cast<const float*>(0x007C5440); }

AnimationSteps& Queue(CUnitServer* self) { return self->queue; }
int& QueueTime(CUnitServer* self) { return self->queue.extra; }
OrderPlan& Plan(CUnitServer* self) { return self->currentPlan; }
AIPath* Motion(CUnitServer* self) { return self->MotionPath(); }
AnimationSteps& MoveSteps(CUnitServer* self) { return self->moveAnimation; }
AnimationSteps& IdleSteps(CUnitServer* self) { return self->idleAnimation; }
std::uint32_t& MoveKey(CUnitServer* self) { return self->moveAnimationKey; }
std::uint32_t& IdleKey(CUnitServer* self) { return self->idleKey; }
int& IdleTime(CUnitServer* self) { return self->idleAnimation.extra; }   // +0x974

// The world time (CWorld::tick_) of an object's world (ObjectWorld) or manager context.
int TickOf(void* world) { return static_cast<CWorld*>(world)->tick_; }
CWorldServer* ContextOf(CUnitServer* self) { return static_cast<CWorldServer*>(ManagerContextOf(self)); }
CPlayer* PlayerOf(CUnit* unit) { return reinterpret_cast<CPlayer*>(unit->player); }
// The unit's server object (CUnit + 0x170) and its vitals.
UnitVitals& ServerVitals(CUnit* unit) { return static_cast<CMapObjectServer*>(unit->companion)->vitals; }

void StartQueueIfUnset(CUnitServer* self) {
    if (QueueTime(self) == -1) QueueTime(self) = TickOf(ObjectWorld(self));
}

// The first step of the queue shown on the unit (00546C10 inlined).
void ShowHead(CUnitServer* self) {
    AnimationSteps& queue = Queue(self);
    if (queue.count == 0) return;
    const AnimationStep& head = queue.steps[0];
    if (head.animation != 0xFF || head.unknown_01 != 0xFF)
        UnitSetAnimation(self->unit, nullptr, head.animation, head.unknown_01, QueueTime(self), head.unknown_02);
}

void DropEffectObject(CUnitServer* self) {
    if (!self->effectObject) return;
    WorldRemoveTimed(WorldServer(), nullptr, self->effectObject->id_);
    CEngineObject* effect = self->effectObject;
    self->effectObject = nullptr;
    if (effect) effect->Release();
}

// The unit stops where it is, facing its direction (the motion restarted with the reversed Y
// axis of its rotation), then stands (0) and takes state 6: the sequence 0054A150 inlines twice.
void FaceAndStop(CUnitServer* self) {
    PathClear(Motion(self));
    int now = TickOf(ObjectWorld(self));
    Vector3 axis;
    QuaternionAxisY(&self->unit->orientation, nullptr, &axis);
    Vector3 back;
    const Vector3* direction = Negate(&back, &axis);
    if (AIPathStartOriginal(Motion(self), nullptr, &self->unit->position, direction, now)) UnitPathSpeeds(self->unit);
    CtlSetStanding(self, nullptr, 0);
    CtlSetState(self, nullptr, kStateChasing);
}

// The unit's movement mode from a record: 3 while it was mode 3 (`wasMode3`), else its mode.
int UnitModeOf(const MoveModeState& state) { return (state.wasMode3 & 1) ? 3 : state.mode; }

// A pause step for an emote (`value` into its +1 byte, open-ended), reusing a trailing one: the
// code 00544D50 is and the idle builders inline.
void AddEmote(AnimationSteps* steps, std::uint8_t value) {
    int count = steps->count;
    if (count == 0 || steps->steps[count - 1].unknown_01 != 0xFF) {
        AnimationStep blank{};
        blank.animation = 0xFF;
        blank.unknown_01 = 0xFF;
        blank.unknown_02 = 0;
        blank.duration = -1;
        blank.unknown_08 = 0;
        StepsSetAtGrow(steps, nullptr, count, &blank);
    }
    AnimationStep& last = steps->steps[steps->count - 1];
    last.unknown_01 = value;
    last.duration = -1;
}

// The weapon and movement bits of the unit's animation key for the current movement mode
// (00550050 | 005500A0, as the idle builders inline them).
std::uint32_t WeaponAndModeBits(CUnitServer* self) {
    return self->WeaponTypeMask() | self->MoveModeMask(&self->moveMode);
}

// The idle animation into `steps` (00550D90's body; 00550F30 / 005474B0 inline it): a walking hero
// stands with the emote 0xFE, the others with their idle animation for the key, if any.
void FillIdle(CUnitServer* self, AnimationSteps* steps) {
    StepsSetSize(steps, nullptr, 0, -1);
    steps->extra = -1;
    if (self->moveMode.mode == kModeRun && self->moveMode.flag4 == 0) {
        AddEmote(steps, kEmoteStand);
        return;
    }
    const auto* entry = reinterpret_cast<const FigureAnimation*>(
        UnitFindAnimation(self->unit, nullptr, WeaponAndModeBits(self) | kKeyIdle, 1));
    if (entry) AddEmote(steps, entry->index);
}

// The idle key of the unit now (weapon and movement bits).
std::uint32_t IdleKeyNow(CUnitServer* self) { return WeaponAndModeBits(self); }

// A motion order on the stack (0x3C bytes): the order header (MotionOrder, 0x20 bytes) and its
// step list (a CList of path cells, vtable 0073EDB0, blocks of 0x80), as 005B22C0
// (AIPath::ToMotionOrder) fills it. The header is zeroed (the original leaves it uninitialised).
struct LocalMotionOrder {
    MotionOrder header;     // +0x00
    PathCellList steps;     // +0x20
    LocalMotionOrder() {
        std::memset(&header, 0, sizeof(header));
        CellListConstruct(&steps, nullptr, 0x80);
    }
    ~LocalMotionOrder() {
        steps.vtable = PathCellListVtable();
        CellListRemoveAll(&steps);
    }
    LocalMotionOrder(const LocalMotionOrder&) = delete;
    LocalMotionOrder& operator=(const LocalMotionOrder&) = delete;
};
static_assert(sizeof(LocalMotionOrder) == 0x3C);

// A CList<CUnit*> as the controller fills them (vtable, head, tail, count, free list, blocks).
using UnitList = mfc::CTypedPtrList<CEngineObject>;

// A player's party: CPlayer::units_ (+0x40), walked as 00455830 (begin) and 004558B0 (the next
// bucket's head) do: stlp::hash_map::First / After.
using PartyMap = CPlayer::ObjectMap;

} // namespace

// --- Small checks -------------------------------------------------------------------------------

bool CUnitServer::CanReactFor(int ticks) {
    if (action.kind == kOrderAttack && state == kStateTurning &&
        TickOf(ObjectWorld(this)) + ticks > nextAttackTime)
        return false;
    return true;
}
EI_REPLACE_METHOD(0x00549D00, CUnitServer::CanReactFor);

// --- Playing an animation ----------------------------------------------------------------------

namespace {
// 00548DE0 / 00548FA0: the two differ only in the state they set.
void PlayByValue(CUnitServer* self, AnimationSteps& steps, int newState) {
    self->state = newState;
    UnitUpdateSizeClass(self->unit);
    DropEffectObject(self);
    AnimationSteps& queue = Queue(self);
    StepsAppend(&queue, nullptr, &steps);
    QueueTime(self) = steps.extra;
    StepsAppend(&queue, nullptr, &steps);
    QueueTime(self) = steps.extra;
    StartQueueIfUnset(self);
    CtlPopFinishedSteps(self);
    ShowHead(self);
    PathClear(Motion(self));
    int now = TickOf(ObjectWorld(self));
    Vector3 axis;
    QuaternionAxisY(&self->unit->orientation, nullptr, &axis);
    Vector3 back = -axis;
    if (AIPathStartOriginal(Motion(self), nullptr, &self->unit->position, &back, now)) UnitPathSpeeds(self->unit);
    // Standing 0 (005467B0 inlined; CUnit +0x278).
    CWorldServer* world = ContextOf(self);
    if (self->unit->obstacle != 0) {
        CSectorMap* sectors = world->Sectors();
        SectorMapRemove(sectors, nullptr, self->unit);
        self->unit->obstacle = 0;
        SectorMapAdd(sectors, nullptr, self->unit);
        UnitMarkChanged(self->unit, nullptr, 7);
    }
    // The list was taken by value: it is destroyed here.
    steps.vtable = kAnimationStepsBaseVtable;
    if (steps.steps) gamecrt::operator_delete(steps.steps);
}
} // namespace

void CUnitServer::PlayAnimationOrder(AnimationSteps steps) { PlayByValue(this, steps, kStateAnimation); }
EI_REPLACE_METHOD(0x00548DE0, CUnitServer::PlayAnimationOrder);

void CUnitServer::PlayEmoteOrder(AnimationSteps steps) { PlayByValue(this, steps, kStateEmote); }
EI_REPLACE_METHOD(0x00548FA0, CUnitServer::PlayEmoteOrder);

// --- Idle and movement animations --------------------------------------------------------------

void CUnitServer::BuildIdle(AnimationSteps* steps) { FillIdle(this, steps); }
EI_REPLACE_METHOD(0x00550D90, CUnitServer::BuildIdle);

AnimationSteps* CUnitServer::IdleAnimation() {
    std::uint32_t key = IdleKeyNow(this);
    if (IdleKey(this) == key) {
        IdleTime(this) = -1;
    } else {
        IdleKey(this) = key;
        FillIdle(this, &IdleSteps(this));
    }
    return &IdleSteps(this);
}
EI_REPLACE_METHOD(0x00550F30, CUnitServer::IdleAnimation);

void CUnitServer::PlayIdle() {
    if (state >= 0 && (state <= 1 || state == kStateTurning)) return;
    std::uint32_t key = IdleKeyNow(this);
    if (IdleKey(this) == key) {
        IdleTime(this) = -1;
    } else {
        IdleKey(this) = key;
        FillIdle(this, &IdleSteps(this));
    }
    // The idle list into the queue (twice), started (00546C40's body inlined).
    StepsAppend(&Queue(this), nullptr, &IdleSteps(this));
    QueueTime(this) = IdleTime(this);
    StepsAppend(&Queue(this), nullptr, &IdleSteps(this));
    QueueTime(this) = IdleTime(this);
    StartQueueIfUnset(this);
    CtlPopFinishedSteps(this);
    ShowHead(this);
}
EI_REPLACE_METHOD(0x005474B0, CUnitServer::PlayIdle);

void CUnitServer::BuildMoveSteps(AnimationSteps* steps, char once) {
    StepsSetSize(steps, nullptr, 0, -1);
    steps->extra = -1;
    CUnit* self = unit;
    std::uint32_t bits;
    switch (static_cast<std::uint32_t>(moveMode.mode)) {
    case 0: bits = 0x30000; break;
    case 1: bits = 0x20000; break;
    case 4: bits = 0x8000; break;
    case 2:
        if (moveMode.flag4 || self->type_ != kUnitTypeHero) {
            bits = 0x10000;
            break;
        }
        [[fallthrough]];
    default:
        // A hero's own walk is looped with the walking bits (not when played once).
        bits = (!once && self->type_ == kUnitTypeHero) ? 0x10000 : 0;
        break;
    }
    std::uint32_t key = WeaponTypeMask() | bits | (self->moveMode == 3 ? 0x100000u : 0x140000u);
    if (once) {
        key |= kKeyMoveOnce;
    } else {
        key |= kKeyMoveLoop;
        if (self->type_ == kUnitTypeHero) {
            float stamina = stats.loadFactor;   // +0x238
            if (!(stamina > HeroWalkLimit())) key |= 0x800000;
            else if (!(stamina > HeroRunLimit())) key |= 0x400000;
        }
    }
    if (const auto* entry = reinterpret_cast<const FigureAnimation*>(UnitFindAnimation(self, nullptr, key, 1))) {
        std::uint8_t index = entry->index;
        if (!once) {
            AddEmote(steps, index);
        } else {
            int duration = CtlAnimationDuration(this, nullptr, index);
            AnimationStep blank{};
            blank.animation = 0xFF;
            blank.unknown_01 = 0xFF;
            blank.unknown_02 = 0;
            blank.duration = -1;
            blank.unknown_08 = 0;
            StepsSetAtGrow(steps, nullptr, steps->count, &blank);
            AnimationStep& last = steps->steps[steps->count - 1];
            last.animation = index;
            last.duration = duration;
        }
    }
    for (int i = 0; i < steps->count; ++i) steps->steps[i].unknown_08 = 2;
}
EI_REPLACE_METHOD(0x005511A0, CUnitServer::BuildMoveSteps);

AnimationSteps* CUnitServer::NextAnimation() {
    CUnit* self = unit;
    std::uint32_t key = WeaponAndModeBits(this);
    if (self->moveMode == 3) {
        key |= 0x100000;
    } else {
        key |= 0x140000;
        if (self->type_ == kUnitTypeHero) {
            float stamina = stats.loadFactor;   // +0x238
            if (!(stamina > HeroWalkLimit())) key |= 0x800000;
            else if (!(stamina > HeroRunLimit())) key |= 0x400000;
        }
    }
    if (MoveKey(this) == key) return &MoveSteps(this);
    MoveKey(this) = key;
    if (self->type_ != kUnitTypeHero) {
        CtlBuildMoveSteps(this, nullptr, &MoveSteps(this), 0);
        return &MoveSteps(this);
    }
    // A hero: the start of the movement once, then the loop appended step by step.
    AnimationSteps loop;
    std::memset(&loop, 0, sizeof(loop));
    loop.vtable = kAnimationStepsVtable;
    CtlBuildMoveSteps(this, nullptr, &loop, 0);
    AnimationSteps& steps = MoveSteps(this);
    CtlBuildMoveSteps(this, nullptr, &steps, 1);
    for (int i = 0; i < loop.count; ++i) {
        AnimationStep step = loop.steps[i];
        if (step.animation == 0xFF) {
            // A pause: an emote of its +1 byte (passed as the misaligned dword at step + 1).
            std::uint32_t value;
            std::memcpy(&value, reinterpret_cast<const std::uint8_t*>(&step) + 1, 4);
            StepsEmote(&steps, nullptr, value);
        } else if (step.unknown_01 == 0xFF) {
            AnimationStep blank;
            StepsAdd(&steps, nullptr, StepBlank(&blank));
            AnimationStep& last = steps.steps[steps.count - 1];
            last.animation = step.animation;
            last.duration = step.duration;
        } else {
            StepsSetAtGrow(&steps, nullptr, steps.count, &step);
        }
    }
    loop.vtable = kAnimationStepsBaseVtable;
    if (loop.steps) gamecrt::operator_delete(loop.steps);
    return &steps;
}
EI_REPLACE_METHOD(0x00551400, CUnitServer::NextAnimation);

// --- Starting the planned action ---------------------------------------------------------------

void CUnitServer::StartAction(char follow, char stepFlags) {
    OrderPlan& plan = Plan(this);
    for (int i = 0; i < plan.animation.count; ++i) plan.animation.steps[i].unknown_02 = static_cast<std::uint8_t>(stepFlags);
    if (state < 0 || (state > 1 && state != kStateTurning)) {
        std::uint32_t key = CtlWeaponTypeMask(this) | CtlMoveModeMask(this, nullptr, &moveMode);
        if (IdleKey(this) == key) {
            IdleTime(this) = -1;
        } else {
            IdleKey(this) = key;
            CtlBuildIdle(this, nullptr, &IdleSteps(this));
        }
        CtlPlaySteps(this, nullptr, &IdleSteps(this));
    }
    state = kStateActing;
    actingCheckTime = TickOf(ManagerContextOf(this)) + 10;
    if (plan.targetId == -1) {
        int now = TickOf(ObjectWorld(this));
        PathTurn(Motion(this), nullptr, now, unit, &plan.direction.x, plan.endTime, plan.startTime);
    } else {
        int until = follow ? plan.endTime : 0;
        int now = TickOf(ObjectWorld(this));
        PathFollow(Motion(this), nullptr, now, unit, plan.targetId, until, plan.startTime);
    }
    UnitPathSpeeds(unit);
    CWorldServer* world = ContextOf(this);
    if (unit->obstacle == 0) return;
    CSectorMap* sectors = world->Sectors();
    SectorMapRemove(sectors, nullptr, unit);
    unit->obstacle = 0;
    SectorMapAdd(sectors, nullptr, unit);
    UnitMarkChanged(unit, nullptr, 7);
}
EI_REPLACE_METHOD(0x005494D0, CUnitServer::StartAction);

// --- Movement modes -----------------------------------------------------------------------------

void CUnitServer::UpdateSneak() {
    CWorldServer* world = ContextOf(this);
    int running = StatsMovementState(&stats, nullptr, 1);
    if (running != 0 && moveMode2.wasMode3 != 0) {
        // Out of breath: back from the second mode's mode 3, with a remark.
        std::uint32_t code = running == 1 ? 0x15 : 0x14;
        if ((code <= 0x29 || code == 0x30) && code != 0xFFFFFFFFu && unit->player)
            PlayerMessage(PlayerOf(unit)->connection_, nullptr, unit, static_cast<int>(code));
        if (action.unknown_20 != 0) action.unknown_20 = 0;
        moveMode2.wasMode3 = 0;
        int mode = moveMode2.mode;
        if (mode != unit->moveMode2) {
            unit->moveMode2 = mode;
            UnitMarkChanged(unit, nullptr, 6);
        }
    }
    if (moveMode.wasMode3 != 0) {
        if (moveMode2.wasMode3 != 0 && MapCheapCell(world->AIMap(), nullptr, unit) && running == 0) {
            sneakWait = 0;
            return;
        }
        int& waited = sneakWait;
        ++waited;
        if (waited <= 5 && moveMode2.wasMode3 != 0) return;
        moveMode.wasMode3 = 0;
        int mode = moveMode.mode;
        if (mode != unit->moveMode) {
            unit->moveMode = mode;
            UnitMarkChanged(unit, nullptr, 6);
        }
        UnitUpdateSizeClass(unit);
        motionChanged = 1;
        return;
    }
    int allowed = unit->player ? PlayerOf(unit)->unknown_108 : 1;
    if (moveMode.mode != kModeRun || moveMode2.wasMode3 == 0) return;
    if (allowed == 0 && !(ServerVitals(unit).mana > 0.0f)) return;   // the breath (vitals +0x14)
    if (!MapCheapCell(world->AIMap(), nullptr, unit)) return;
    if (running != 0) return;
    sneakWait = 0;
    moveMode.wasMode3 = 1;
    int mode = UnitModeOf(moveMode);
    if (mode != unit->moveMode) {
        unit->moveMode = mode;
        UnitMarkChanged(unit, nullptr, 6);
    }
    UnitUpdateSizeClass(unit);
    motionChanged = 1;
}
EI_REPLACE_METHOD(0x00548520, CUnitServer::UpdateSneak);

bool CUnitServer::ChangeMoveMode() {
    if (moveMode.mode == moveMode2.mode && moveMode.flag4 == moveMode2.flag4) return false;
    bool modeChanged = moveMode.mode != moveMode2.mode;
    MoveModeState saved = moveMode;
    SectorMapRemove(ServerSectors(), nullptr, unit);
    bool passableBefore = MapCellPassable(ServerAIMap(), nullptr, unit);
    bool sameMode = moveMode.mode == moveMode2.mode;
    moveMode = moveMode2;
    unit->moveMode = UnitModeOf(moveMode);
    UnitUpdateSizeClass(unit);
    bool placed;
    if (!sameMode &&
        !(passableBefore ? MapCellPassable(ServerAIMap(), nullptr, unit) : AIMapCanLeaveCellOriginal(ServerAIMap(), nullptr, unit)))
        placed = false;
    else
        placed = SectorMapAdd(ServerSectors(), nullptr, unit);
    UnitMarkChanged(unit, nullptr, 6);
    if (!placed) {
        // The new mode does not fit here: back to the old one, and the party is told.
        moveMode = saved;
        unit->moveMode = UnitModeOf(moveMode);
        moveMode2.mode = moveMode.mode;
        if (moveMode.mode != 2) moveMode2.wasMode3 = 0;
        unit->moveMode2 = UnitModeOf(moveMode2);
        UnitUpdateSizeClass(unit);
        SectorMapAdd(ServerSectors(), nullptr, unit);
        CUnit* self = unit;
        if (self->player) PlayerMessage(PlayerOf(self)->connection_, nullptr, self, kMessageModeBlocked);
        return false;
    }
    if (state == kStateIdle) {
        if (modeChanged) FaceAndStop(this);
        if (state == kStateIdle && !modeChanged) {
            UnitUpdateSizeClass(unit);
            if (action2.kind == kOrderNone) action2.CallAssign(&action);
            return true;
        }
    }
    if (action.kind != kOrderAttack) {
        AnimationSteps transition;
        std::memset(&transition, 0, sizeof(transition));
        transition.vtable = kAnimationStepsVtable;
        if (CtlBuildTransition(this, nullptr, &transition, &saved, &moveMode)) {
            FaceAndStop(this);
            // The transition is played as an animation order (a by-value copy, built as the
            // original does: SetSize, then the steps one by one).
            AnimationSteps copy;
            StepsConstruct(&copy);
            copy.vtable = kAnimationStepsVtable;
            StepsSetSize(&copy, nullptr, transition.count, -1);
            for (int i = 0; i < transition.count; ++i) copy.steps[i] = transition.steps[i];
            copy.extra = transition.extra;
            CtlPlayAnimationOrder(this, nullptr, copy);
        }
        transition.vtable = kAnimationStepsBaseVtable;
        if (transition.steps) gamecrt::operator_delete(transition.steps);
    }
    UnitUpdateSizeClass(unit);
    if (action2.kind == kOrderNone) action2.CallAssign(&action);
    return true;
}
EI_REPLACE_METHOD(0x0054A150, CUnitServer::ChangeMoveMode);

// --- Paths --------------------------------------------------------------------------------------

bool CUnitServer::PathTo(const float* point, float range) {
    ManagerContextOf(this);
    CtlUpdateSneak(this);
    motionChanged = 0;
    stoodBlocked = 0;
    PathClear(Motion(this));
    std::uint32_t keep = unit->altPassCosts;
    CWorldServer* world = ContextOf(this);
    Motion(this)->flag6E = unit->player ? 0 : 1;   // +0xAA
    std::uint32_t before = unit->altPassCosts;
    unit->altPassCosts = keep;
    bool found = MapFindPath(world->AIMap(), nullptr, unit, point, Motion(this), range, 0.0f);
    unit->altPassCosts = before;
    if (!found) {
        // +0xA9, read as a byte: the original compares it with 1.
        std::uint8_t blocked = reinterpret_cast<const std::uint8_t&>(Motion(this)->failed);
        movingAnimation = 0;
        CtlStandStill(this);
        CtlSetState(this, nullptr, kStateChasing);
        stoodBlocked = blocked == 1 ? 1 : 0;
        return false;
    }
    Motion(this)->goal.z = range;   // +0xA4: the range (Replan reads it back)
    sneakWait = 0;
    UnitPathSpeeds(unit);
    if (state < 0 || (state > 1 && state != kStateTurning)) CtlPlaySteps(this, nullptr, CtlIdleAnimation(this));
    state = kStateIdle;
    UnitUpdateSizeClass(unit);
    DropEffectObject(this);
    if (movingAnimation != 0) {
        // Moving already: the movement animation goes on unless it is the same one-step loop.
        AnimationSteps* next = CtlNextAnimation(this);
        AnimationSteps moving;
        StepsCopyConstruct(&moving, nullptr, next);
        moving.extra = next->extra;
        AnimationSteps current;
        StepsConstruct(&current);
        current.vtable = kAnimationStepsVtable;
        StepsAppend(&current, nullptr, &Queue(this));
        current.extra = QueueTime(this);
        if (current.count != 1 || moving.count != 1 || current.steps[0].unknown_01 != moving.steps[0].unknown_01) {
            StepsAppend(&Queue(this), nullptr, &moving);
            QueueTime(this) = moving.extra;
            StepsCopyB(&Queue(this), nullptr, &moving);
            StartQueueIfUnset(this);
            CtlPopFinishedSteps(this);
            CtlShowCurrentStep(this);
        }
        current.vtable = kAnimationStepsBaseVtable;
        if (current.steps) gamecrt::operator_delete(current.steps);
        moving.vtable = kAnimationStepsBaseVtable;
        if (moving.steps) gamecrt::operator_delete(moving.steps);
    }
    return true;
}
EI_REPLACE_METHOD(0x00548700, CUnitServer::PathTo);

void CUnitServer::LiftNearby(void* listIn, const float* point, char moving) {
    auto* list = static_cast<UnitList*>(listIn);
    CWorldServer* world = ContextOf(this);
    CPlayer* player = PlayerOf(unit);
    // RemoveAll.
    for (UnitList::Node* node = list->head; node; node = node->next) {
    }
    list->count = 0;
    list->free = nullptr;
    list->tail = nullptr;
    list->head = nullptr;
    PlexFreeDataChain(list->blocks);
    list->blocks = nullptr;
    if (!player || player->sessionKind100_ != 1) return;
    // Away from the point: the units near it that are not in the player's group (+0x510C of its
    // messages object) are lifted off the sector map and stay in the list.
    float dx = point[0] - unit->position.x;
    float dy = point[1] - unit->position.y;
    float dz = point[2] - unit->position.z;
    if (LengthSquaredUnrounded<Sum3::ZYX>(dx, dy, dz) > *reinterpret_cast<const float*>(0x0073CA98)) {
        CSectorMap* sectors = world->Sectors();
        SectorMapFindUnitsInRadius(sectors, nullptr, point, list, 4.0f);
        void* group = &reinterpret_cast<CPlayerServer*>(player->connection_)->partyUnits_;
        for (UnitList::Node* node = list->head; node;) {
            UnitList::Node* current = node;
            node = node->next;
            CEngineObject* other = current->data;
            if (GroupContains(reinterpret_cast<const ScriptGroup*>(group), nullptr, other) == 0) {
                SectorMapRemove(sectors, nullptr, other);
                continue;
            }
            if (current == list->head) list->head = current->next;
            else current->prev->next = current->next;
            if (current == list->tail) list->tail = current->prev;
            else current->next->prev = current->prev;
            ListFreeNode(list, nullptr, current);
        }
    }
    // The party members moving (when `moving`) or following this unit are lifted and listed too.
    const PartyMap* party = &player->units_;
    for (PartyMap::Node* node = party->First(); node; node = party->After(node)) {
        auto* member = static_cast<CUnit*>(node->value);
        if (!member) break;
        if (member == unit) continue;
        auto* controller = reinterpret_cast<CUnitServer*>(member->controller);
        bool lift = moving && (controller->action.kind == kOrderMove || controller->action2.kind == kOrderMove);
        if (!lift) lift = BrainLeaderOrFollowedOriginal(&controller->brain) == unit;
        if (!lift) continue;
        auto* added = static_cast<UnitList::Node*>(ListNewNode(list, nullptr, list->tail, nullptr));
        added->data = member;
        if (list->tail) list->tail->next = added;
        else list->head = added;
        list->tail = added;
        SectorMapRemove(ServerSectors(), nullptr, member);
    }
}
EI_REPLACE_METHOD(0x005468D0, CUnitServer::LiftNearby);

void CUnitServer::ReportPath(void* pathIn, char partial) {
    if (brain.action == AIBrain::kActionFollow) return;   // following reports nothing
    auto* path = static_cast<AIPath*>(pathIn);
    CUnit* self = unit;
    const float* goal = &path->goal.x;
    // The path as a motion order for the message. The original leaves its header uninitialised
    // when the path is not partial (the message does not read it for kind 3).
    LocalMotionOrder end;
    int message;
    if (!partial) {
        if (self->player) PlayerPathMessage(PlayerOf(self)->connection_, nullptr, self, &end, goal, 3, 1, kFarBits);
        if (BrainLeaderOrFollowedOriginal(&brain)) return;
        self = unit;
        if (!self->player) return;
        message = kMessageCannotDo;
    } else {
        PathToMotionOrder(path, nullptr, &end);
        float dy = self->position.y - goal[1];
        float dx = self->position.x - goal[0];
        float reached = LengthSquared2DUnrounded(dx, dy);
        float half = HalfCell();
        float ey = half * path->targetY - goal[1];
        float ex = half * path->targetX - goal[0];
        float left = LengthSquared2DUnrounded(ex, ey);
        bool far = reached * *reinterpret_cast<const float*>(0x0073F0B8) > left;   // unordered: not far
        if (self->player)
            PlayerPathMessage(PlayerOf(self)->connection_, nullptr, self, &end, goal, 0, far ? 0 : 1, kFarBits);
        if (BrainLeaderOrFollowedOriginal(&brain)) return;
        self = unit;
        if (!self->player) return;
        message = far ? kMessagePartial : kMessageCannotDo;
    }
    PlayerMessage(PlayerOf(self)->connection_, nullptr, self, message);
}
EI_REPLACE_METHOD(0x0054EB30, CUnitServer::ReportPath);

// --- Resync with the unit ---------------------------------------------------------------------

namespace {
// 0052B030 a reference's release, 0052A8F0 its assignment; 00544E70 does the queue hold `steps`.
void __fastcall ReferenceRelease(CEngineObject** a0) { CountedRefResetOriginal((void*)a0); }
void __fastcall ReferenceSet(CEngineObject** a0, void*, CEngineObject* a2) { CountedRefAssignOriginal((void*)a0, nullptr, (void*)a2); }
bool __fastcall QueueHolds(AnimationSteps* a0, void*, const AnimationSteps* a2) { return StepsQueueHoldsOriginal((void*)a0, nullptr, (const void*)a2); }
IsaacRandom& Random() { return IsaacRandom::Global(); }   // 007A9D40

// A movement record from the unit's mode: mode 3 is mode 2 with `wasMode3`.
void RecordFromMode(MoveModeState* record, int mode) {
    record->wasMode3 = 0;
    record->mode = mode;
    if (mode == 3) {
        record->mode = 2;
        record->wasMode3 = 1;
    }
}
} // namespace

void CUnitServer::ResyncWithUnit() {
    action.kind = kOrderNone;
    if (action.target) action.target->Release();
    action.target = nullptr;
    action.flag18 = 0;
    action.unknown_1C = 0;
    action.unknown_20 = 0;
    if (action.kind != 0 && action2.kind != 7 && action2.kind != 8) {   // the first is always true
        action2.kind = kOrderNone;
        ReferenceRelease(&action2.target);
        action2.flag18 = 0;
        action2.unknown_1C = 0;
        action2.unknown_20 = 0;
        action2.kind = 0;
        ReferenceSet(&action2.target, nullptr, nullptr);
        action2.unknown_20 = 0;
    }
    RecordFromMode(&moveMode, unit->moveMode);
    RecordFromMode(&moveMode2, unit->moveMode2);
    PathClear(Motion(this));
    int now = TickOf(ObjectWorld(this));
    Vector3 axis;
    QuaternionAxisY(&unit->orientation, nullptr, &axis);
    Vector3 back;
    const Vector3* direction = Negate(&back, &axis);
    if (AIPathStartOriginal(Motion(this), nullptr, &unit->position, direction, now)) UnitPathSpeeds(unit);
    CtlSetStanding(this, nullptr, 0);
    CtlSetState(this, nullptr, kStateChasing);
    UnitUpdateSizeClass(unit);
    unit->pendingWeapon = 0;
    if (unit->pendingWeapon != static_cast<int>(unit->activeWeapon))
        unit->Controller()->ShowWeapon(static_cast<std::uint32_t>(unit->pendingWeapon));   // slot 25
    if (state == kStateTurning) return;
    std::uint32_t key = CtlWeaponTypeMask(this) | CtlMoveModeMask(this, nullptr, &moveMode);
    if (IdleKey(this) == key) {
        IdleTime(this) = -1;
    } else {
        IdleKey(this) = key;
        CtlBuildIdle(this, nullptr, &IdleSteps(this));
    }
    if (!QueueHolds(&Queue(this), nullptr, &IdleSteps(this))) {
        // The idle animation starts at a random phase (up to 50 ticks back) on an empty queue.
        if (Queue(this).count == 0 && QueueTime(this) == -1)
            IdleTime(this) = TickOf(ObjectWorld(this)) - static_cast<int>(Random().Next() % 0x33);
        StepsCopyB(&Queue(this), nullptr, &IdleSteps(this));
        StepsCopyB(&Queue(this), nullptr, &IdleSteps(this));
        StartQueueIfUnset(this);
        CtlPopFinishedSteps(this);
        CtlShowCurrentStep(this);
    }
    CtlStandStill(this);
    CtlSetState(this, nullptr, kStateTurning);
}
EI_REPLACE_METHOD(0x00554450, CUnitServer::ResyncWithUnit);

// --- Reset ---------------------------------------------------------------------------------------

void CUnitServer::ResetController() {
    auto releaseRef = [](CEngineObject*& ref) {   // 0052B030
        if (ref) ref->Release();
        ref = nullptr;
    };
    action.kind = kOrderNone;
    releaseRef(action.target);
    action.flag18 = 0;
    action.unknown_1C = 0;
    action.unknown_20 = 0;
    action2.kind = kOrderNone;
    releaseRef(action2.target);
    action2.flag18 = 0;
    action2.unknown_1C = 0;
    action2.unknown_20 = 0;
    unknown_020 = 0;
    unknown_021 = 0;
    StepsSetSize(&Queue(this), nullptr, 0, -1);
    QueueTime(this) = -1;
    PathClear(Motion(this));
    OrderPlan& plan = Plan(this);
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
    std::memset(&pendingHit, 0, 0x3C);
    pendingHit.multiplier = 1.0f;   // +0x130
    StepsSetSize(&savedQueue, nullptr, 0, -1);
    savedQueue.extra = -1;
    state = kStateChasing;
    hitDirection = 0xFFFFFFFFu;
    keepHitDirection = 0;
    stoodBlocked = 0;
    motionChanged = 0;
    movingAnimation = 0;
    moveMode.mode = 2;
    moveMode.flag4 = 0;
    moveMode.wasMode3 = 0;
    moveMode2.mode = 2;
    moveMode2.flag4 = 0;
    moveMode2.wasMode3 = 0;
    CEngineObject* effect = effectObject;
    effectObject = nullptr;
    if (effect) effect->Release();
    actingCheckTime = -1;
    action.kind = 0;
    action2.kind = kOrderNone;
    nextAttackTime = 0;
    unknown_188 = 0;
    unknown_18C = 0;
    unknown_18D = 0;
    followsLeader = 0;
    CEngineObject* waited = followedLeader;
    followedLeader = nullptr;
    if (waited) waited->Release();
    unknown_18E = 0;
    BrainResetAll(&brain);
    AlarmReset(&voice);
    unknown_918 = nullptr;
    moveFactors[0] = 1.0f;
    moveFactors[2] = 1.0f;
    moveFactors[3] = 1.0f;
    moveFactors[1] = 1.0f;
    UnitStatsResetOriginal(&stats);
    tameStage = 0;
    ownSide = -1;
    tamerSide = -1;
    speedClass = 0xF;
    claimedHostile = 0;
    unknown_8F4 = 0.0f;
    MoveKey(this) = 0x666DEAD;
    StepsSetSize(&MoveSteps(this), nullptr, 0, -1);
    MoveSteps(this).extra = -1;
    IdleKey(this) = 0x666DEAD;
    StepsSetSize(&IdleSteps(this), nullptr, 0, -1);
    IdleSteps(this).extra = -1;
    unknown_938 = 0;
    // The armour spell list (+0x91C) emptied: each element's item released (00552B80, the
    // instantiation's DestructElements), then count, free list, tail, head and blocks cleared.
    for (auto* node = armourSpells.head; node; node = node->next)
        if (node->data.item) node->data.item->Release();
    armourSpells.RemoveAll();
    actionDone = nullptr;
}
EI_REPLACE_METHOD(0x00546480, CUnitServer::ResetController);
