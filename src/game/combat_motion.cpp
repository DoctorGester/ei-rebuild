// Combat, seventh part: the controller's movement modes, idle and movement animations, paths to a
// point, the start of a planned action and the controller's reset. See combat.h, combat_plan.h
// and docs/notes/combat.md ("Fourth round").
//
// Animation lists cached on the controller: +0x944 the movement animation (key +0x940), +0x960
// the idle animation (key +0x95C; its start time +0x974).
#include "combat.h"
#include "combat_internal.h"
#include "combat_plan.h"

#include "item.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

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
constexpr int kSlotRelease = 4;

// --- Forwarders (thiscall as __fastcall with the unused EDX) --------------------------------------

__declspec(naked) void* __fastcall ContextOf(const void*) EI_ORIGINAL(0x00455940)
__declspec(naked) void* __fastcall WorldOf(const void*) EI_ORIGINAL(0x00455950)
// CUnit: 0050E9F0 MarkChanged, 0052EA90 the animation bytes, 00530570 the size class, 005301B0
// the animation entry for a key (combat_steps.cpp), 0052E9D0 the path speeds.
__declspec(naked) void __fastcall UnitMarkChanged(CUnit*, void*, int) EI_ORIGINAL(0x0050E9F0)
__declspec(naked) void __fastcall UnitSetAnimation(CUnit*, void*, int, int, int, int) EI_ORIGINAL(0x0052EA90)
__declspec(naked) void __fastcall UnitUpdateSizeClass(CUnit*) EI_ORIGINAL(0x00530570)
__declspec(naked) char* __fastcall UnitFindAnimation(CUnit*, void*, std::uint32_t, int) EI_ORIGINAL(0x005301B0)
__declspec(naked) void __fastcall UnitPathSpeeds(CUnit*) EI_ORIGINAL(0x0052E9D0)
// 0040F560 Quaternion::AxisY, 00402200 NegateVector (core_maths.cpp).
__declspec(naked) float* __fastcall QuaternionAxisY(const void*, void*, float*) EI_ORIGINAL(0x0040F560)
__declspec(naked) float* __fastcall Negate(float* /*out*/, const float* /*v*/) EI_ORIGINAL(0x00402200)
// UnitStats (unit_vitals.cpp / unit_stats_pack.cpp): 005241C0 the stamina state for a movement
// (1: running), 005233E0 Reset.
__declspec(naked) int __fastcall StatsMovementState(UnitStats*, void*, int) EI_ORIGINAL(0x005241C0)
__declspec(naked) void __fastcall StatsReset(UnitStats*) EI_ORIGINAL(0x005233E0)
// The AI map (world + 0x170, ai_map.cpp / ai_movement.cpp / ai_path.cpp): 005BA6E0
// IsUnitCellPassable, 005BA830 CanLeaveCell, 005BA5A0 StandsOnCheapCell, 005BB350 FindPath.
__declspec(naked) bool __fastcall MapCellPassable(void*, void*, CUnit*) EI_ORIGINAL(0x005BA6E0)
__declspec(naked) bool __fastcall MapCanLeaveCell(void*, void*, CUnit*) EI_ORIGINAL(0x005BA830)
__declspec(naked) bool __fastcall MapCheapCell(void*, void*, CUnit*) EI_ORIGINAL(0x005BA5A0)
__declspec(naked) bool __fastcall MapFindPath(void*, void*, CUnit*, const void* /*target*/, void* /*path*/, float /*range*/,
                                              float /*minDistance*/) EI_ORIGINAL(0x005BB350)
// The sector map (world + 0x294 = 007B01F4): 005C1010 Remove, 005C0DF0 Add (returns AL),
// 005C0BD0 FindUnitsInRadius.
__declspec(naked) void __fastcall SectorRemove(void*, void*, CEngineObject*) EI_ORIGINAL(0x005C1010)
__declspec(naked) bool __fastcall SectorAdd(void*, void*, CEngineObject*) EI_ORIGINAL(0x005C0DF0)
__declspec(naked) void __fastcall SectorFind(void*, void*, const float*, void* /*list*/, float /*radius*/)
    EI_ORIGINAL(0x005C0BD0)
// 004B3330 ScriptGroup::Contains (script_group.cpp).
__declspec(naked) int __fastcall GroupContains(void*, void*, CEngineObject*) EI_ORIGINAL(0x004B3330)
// The motion (+0x3C, an AIPath; ai_motion.cpp / ai_path_stand.cpp): 005B23A0 Clear, 005C7B00 Start,
// 005B1F10 follow a target, 005B2050 turn to a direction, 005B22C0 the path's end point.
__declspec(naked) void __fastcall PathClear(void*) EI_ORIGINAL(0x005B23A0)
__declspec(naked) bool __fastcall PathStart(void*, void*, const float*, const float*, int) EI_ORIGINAL(0x005C7B00)
__declspec(naked) void __fastcall PathFollow(void*, void*, int /*now*/, CUnit*, int /*targetId*/, int /*until*/,
                                             int /*start*/) EI_ORIGINAL(0x005B1F10)
__declspec(naked) void __fastcall PathTurn(void*, void*, int /*now*/, CUnit*, const float* /*direction*/,
                                           int /*until*/, int /*start*/) EI_ORIGINAL(0x005B2050)
__declspec(naked) void __fastcall PathEndPoint(void*, void*, float* /*out*/) EI_ORIGINAL(0x005B22C0)
// The world server's timed objects (world_server.cpp).
__declspec(naked) void __fastcall WorldRemoveTimed(void*, void*, int) EI_ORIGINAL(0x0058E5D0)
// The AI: 005D5E90 the brain's leader (unit_ai.cpp), 005D4F70 the brain's reset, 005AEA40 the
// alarm's reset.
__declspec(naked) CUnit* __fastcall BrainLeader(void*) EI_ORIGINAL(0x005D5E90)
__declspec(naked) void __fastcall BrainResetAll(void*) EI_ORIGINAL(0x005D4F70)
__declspec(naked) void __fastcall AlarmReset(void*) EI_ORIGINAL(0x005AEA40)
// 0066D490: the player's message about a path to `point` (kind, flag, distance limit).
__declspec(naked) void __fastcall PlayerPathMessage(void*, void*, CUnit*, const float* /*from*/, const float* /*point*/,
                                                    int /*kind*/, int /*flag*/, std::uint32_t /*limitBits*/)
    EI_ORIGINAL(0x0066D490)
// The controller's functions rewritten elsewhere, through their game addresses (combat_update.cpp,
// combat_steps.cpp, combat_tick.cpp).
__declspec(naked) bool __fastcall CtlPopFinishedSteps(CUnitServer*) EI_ORIGINAL(0x00546B40)
__declspec(naked) void __fastcall CtlShowCurrentStep(CUnitServer*) EI_ORIGINAL(0x00546C10)
__declspec(naked) void __fastcall CtlPlaySteps(CUnitServer*, void*, const AnimationSteps*) EI_ORIGINAL(0x00546C40)
__declspec(naked) void __fastcall CtlSetState(CUnitServer*, void*, int) EI_ORIGINAL(0x00547090)
__declspec(naked) void __fastcall CtlStandStill(CUnitServer*) EI_ORIGINAL(0x00547280)
__declspec(naked) void __fastcall CtlSetStanding(CUnitServer*, void*, int) EI_ORIGINAL(0x005467B0)
__declspec(naked) bool __fastcall CtlBuildTransition(CUnitServer*, void*, AnimationSteps*, const MoveModeState*,
                                                     const MoveModeState*) EI_ORIGINAL(0x005516D0)
__declspec(naked) int __fastcall CtlAnimationDuration(CUnitServer*, void*, std::uint32_t) EI_ORIGINAL(0x00551A40)
__declspec(naked) std::uint32_t __fastcall CtlWeaponTypeMask(CUnitServer*) EI_ORIGINAL(0x00550050)
__declspec(naked) std::uint32_t __fastcall CtlMoveModeMask(CUnitServer*, void*, const MoveModeState*) EI_ORIGINAL(0x005500A0)
// And the functions of this file.
__declspec(naked) void __fastcall CtlUpdateSneak(CUnitServer*) EI_ORIGINAL(0x00548520)
__declspec(naked) AnimationSteps* __fastcall CtlIdleAnimation(CUnitServer*) EI_ORIGINAL(0x00550F30)
__declspec(naked) void __fastcall CtlBuildIdle(CUnitServer*, void*, AnimationSteps*) EI_ORIGINAL(0x00550D90)
__declspec(naked) void __fastcall CtlBuildMoveSteps(CUnitServer*, void*, AnimationSteps*, int) EI_ORIGINAL(0x005511A0)
__declspec(naked) AnimationSteps* __fastcall CtlNextAnimation(CUnitServer*) EI_ORIGINAL(0x00551400)
__declspec(naked) void __fastcall CtlPlayAnimation(CUnitServer*, void*, AnimationSteps) EI_ORIGINAL(0x00548DE0)
// Animation lists (MFC CArray<AnimationStep> instantiations, library code): 00556950 / 005483C0
// Copy, 00556870 copy constructor, 00557B60 constructor, 00557E70 destructor, 00557B80 SetSize,
// 00558950 SetAtGrow, 00557CC0 Add, 00544D50 add an emote (a pause step's +1 byte); 00550A60 a
// blank step; 00544FD0 OrderPlan's reset.
__declspec(naked) void __fastcall StepsCopy(AnimationSteps*, void*, const AnimationSteps*) EI_ORIGINAL(0x00556950)
__declspec(naked) void __fastcall StepsCopyB(AnimationSteps*, void*, const AnimationSteps*) EI_ORIGINAL(0x005483C0)
__declspec(naked) AnimationSteps* __fastcall StepsCopyConstruct(AnimationSteps*, void*, const AnimationSteps*)
    EI_ORIGINAL(0x00556870)
__declspec(naked) AnimationSteps* __fastcall StepsConstruct(AnimationSteps*) EI_ORIGINAL(0x00557B60)
__declspec(naked) void __fastcall StepsDestroy(AnimationSteps*) EI_ORIGINAL(0x00557E70)
__declspec(naked) void __fastcall StepsSetSize(AnimationSteps*, void*, int, int) EI_ORIGINAL(0x00557B80)
__declspec(naked) void __fastcall StepsSetAtGrow(AnimationSteps*, void*, int, const AnimationStep*) EI_ORIGINAL(0x00558950)
__declspec(naked) int __fastcall StepsAdd(AnimationSteps*, void*, const AnimationStep*) EI_ORIGINAL(0x00557CC0)
__declspec(naked) void __fastcall StepsEmote(AnimationSteps*, void*, std::uint32_t) EI_ORIGINAL(0x00544D50)
__declspec(naked) AnimationStep* __fastcall StepBlank(AnimationStep*) EI_ORIGINAL(0x00550A60)
__declspec(naked) void __fastcall PlanReset(OrderPlan*) EI_ORIGINAL(0x00544FD0)
// MFC: CPlex::FreeDataChain (006FE90F); the CList<CUnit*> node allocation (0052CEF0, NewNode(prev,
// next)) and release (005588A0, FreeNode) of the lists the controller fills.
__declspec(naked) void __fastcall PlexFreeDataChain(void*) EI_ORIGINAL(0x006FE90F)
__declspec(naked) void* __fastcall ListNewNode(void*, void*, void* /*prev*/, void* /*next*/) EI_ORIGINAL(0x0052CEF0)
__declspec(naked) void __fastcall ListFreeNode(void*, void*, void*) EI_ORIGINAL(0x005588A0)

void* const kWorldServer = reinterpret_cast<void*>(0x007AFF60);
void* const kAIMap = reinterpret_cast<void*>(0x007B00D0);        // the world server + 0x170
void* const kSectorMap = reinterpret_cast<void*>(0x007B01F4);    // the world server + 0x294
float HalfCell() { return *reinterpret_cast<const float*>(0x0073EEE4) * *reinterpret_cast<const float*>(0x0073B86C); }
float HeroWalkLimit() { return *reinterpret_cast<const float*>(0x007C5448); }
float HeroRunLimit() { return *reinterpret_cast<const float*>(0x007C5440); }

AnimationSteps& Queue(CUnitServer* self) { return At<AnimationSteps>(self, 0x24); }
int& QueueTime(CUnitServer* self) { return Queue(self).extra; }
OrderPlan& Plan(CUnitServer* self) { return At<OrderPlan>(self, 0xC4); }
void* Motion(CUnitServer* self) { return reinterpret_cast<char*>(self) + 0x3C; }
AnimationSteps& MoveSteps(CUnitServer* self) { return At<AnimationSteps>(self, 0x944); }
AnimationSteps& IdleSteps(CUnitServer* self) { return At<AnimationSteps>(self, 0x960); }
std::uint32_t& MoveKey(CUnitServer* self) { return At<std::uint32_t>(self, 0x940); }
std::uint32_t& IdleKey(CUnitServer* self) { return At<std::uint32_t>(self, 0x95C); }
int& IdleTime(CUnitServer* self) { return At<int>(self, 0x974); }   // IdleSteps(self).extra

void StartQueueIfUnset(CUnitServer* self) {
    if (QueueTime(self) == -1) QueueTime(self) = At<int>(WorldOf(self), 0x64);
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
    WorldRemoveTimed(kWorldServer, nullptr, self->effectObject->id_);
    CEngineObject* effect = self->effectObject;
    self->effectObject = nullptr;
    if (effect) ei::VCall<void>(effect, kSlotRelease);
}

// The unit stops where it is, facing its direction (the motion restarted with the reversed Y
// axis of its rotation), then stands (0) and takes state 6: the sequence 0054A150 inlines twice.
void FaceAndStop(CUnitServer* self) {
    PathClear(Motion(self));
    int now = At<int>(WorldOf(self), 0x64);
    float axis[3];
    QuaternionAxisY(&At<float>(self->unit, 0x34), nullptr, axis);
    float back[3];
    const float* direction = Negate(back, axis);
    if (PathStart(Motion(self), nullptr, &self->unit->positionX, direction, now)) UnitPathSpeeds(self->unit);
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
    char* entry = UnitFindAnimation(self->unit, nullptr, WeaponAndModeBits(self) | kKeyIdle, 1);
    if (entry) AddEmote(steps, At<std::uint8_t>(entry, 0x10));
}

// The idle key of the unit now (weapon and movement bits).
std::uint32_t IdleKeyNow(CUnitServer* self) { return WeaponAndModeBits(self); }

// A CList<CUnit*> as the controller fills them (vtable, head, tail, count, free list, blocks).
struct UnitList {
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
};

// A player's party: an STLport hash_map<id, unit> at player + 0x40 (00455830 begin, 004558B0 the
// next bucket's head).
struct PartyNode {
    PartyNode* next;
    std::uint32_t key;
    CUnit* unit;
};
struct PartyMap {
    std::uint32_t functors;
    PartyNode** buckets;
    PartyNode** finish;
};
std::uint32_t BucketCount(const PartyMap* map) {
    return static_cast<std::uint32_t>(static_cast<int>(reinterpret_cast<std::uintptr_t>(map->finish) -
                                                       reinterpret_cast<std::uintptr_t>(map->buckets)) >> 2);
}
PartyNode* FirstNode(const PartyMap* map) {
    for (std::uint32_t i = 0; i < BucketCount(map); ++i)
        if (map->buckets[i]) return map->buckets[i];
    return nullptr;
}
PartyNode* NextNode(const PartyMap* map, const PartyNode* node) {
    if (node->next) return node->next;
    std::uint32_t bucket = node->key % BucketCount(map);
    for (;;) {
        ++bucket;
        if (BucketCount(map) <= bucket) return nullptr;
        if (PartyNode* head = map->buckets[bucket]) return head;
    }
}

} // namespace

// --- Small checks -------------------------------------------------------------------------------

bool CUnitServer::CanReactFor(int ticks) {
    if (action.kind == kOrderAttack && state == kStateTurning &&
        At<int>(WorldOf(this), 0x64) + ticks > At<int>(this, 0x184))
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
    StepsCopy(&queue, nullptr, &steps);
    QueueTime(self) = steps.extra;
    StepsCopy(&queue, nullptr, &steps);
    QueueTime(self) = steps.extra;
    StartQueueIfUnset(self);
    CtlPopFinishedSteps(self);
    ShowHead(self);
    PathClear(Motion(self));
    int now = At<int>(WorldOf(self), 0x64);
    float axis[3];
    QuaternionAxisY(&At<float>(self->unit, 0x34), nullptr, axis);
    float back[3] = {-axis[0], -axis[1], -axis[2]};
    if (PathStart(Motion(self), nullptr, &self->unit->positionX, back, now)) UnitPathSpeeds(self->unit);
    // Standing 0 (005467B0 inlined).
    void* world = ContextOf(self);
    int& standing = At<int>(self->unit, 0x278);
    if (standing != 0) {
        void* sectors = static_cast<char*>(world) + 0x294;
        SectorRemove(sectors, nullptr, self->unit);
        At<int>(self->unit, 0x278) = 0;
        SectorAdd(sectors, nullptr, self->unit);
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
    StepsCopy(&Queue(this), nullptr, &IdleSteps(this));
    QueueTime(this) = IdleTime(this);
    StepsCopy(&Queue(this), nullptr, &IdleSteps(this));
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
            float stamina = At<float>(this, 0x238);
            if (!(stamina > HeroWalkLimit())) key |= 0x800000;
            else if (!(stamina > HeroRunLimit())) key |= 0x400000;
        }
    }
    if (char* entry = UnitFindAnimation(self, nullptr, key, 1)) {
        std::uint8_t index = At<std::uint8_t>(entry, 0x10);
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
            float stamina = At<float>(this, 0x238);
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
    At<int>(this, 0x180) = At<int>(ContextOf(this), 0x64) + 10;
    if (plan.targetId == -1) {
        int now = At<int>(WorldOf(this), 0x64);
        PathTurn(Motion(this), nullptr, now, unit, plan.direction, plan.endTime, plan.startTime);
    } else {
        int until = follow ? plan.endTime : 0;
        int now = At<int>(WorldOf(this), 0x64);
        PathFollow(Motion(this), nullptr, now, unit, plan.targetId, until, plan.startTime);
    }
    UnitPathSpeeds(unit);
    void* world = ContextOf(this);
    if (At<int>(unit, 0x278) == 0) return;
    void* sectors = static_cast<char*>(world) + 0x294;
    SectorRemove(sectors, nullptr, unit);
    At<int>(unit, 0x278) = 0;
    SectorAdd(sectors, nullptr, unit);
    UnitMarkChanged(unit, nullptr, 7);
}
EI_REPLACE_METHOD(0x005494D0, CUnitServer::StartAction);

// --- Movement modes -----------------------------------------------------------------------------

void CUnitServer::UpdateSneak() {
    void* world = ContextOf(this);
    int running = StatsMovementState(&stats, nullptr, 1);
    if (running != 0 && moveMode2.wasMode3 != 0) {
        // Out of breath: back from the second mode's mode 3, with a remark.
        std::uint32_t code = running == 1 ? 0x15 : 0x14;
        if ((code <= 0x29 || code == 0x30) && code != 0xFFFFFFFFu && unit->player)
            PlayerMessage(At<void*>(unit->player, 0x20), nullptr, unit, static_cast<int>(code));
        if (action.unknown_20 != 0) action.unknown_20 = 0;
        moveMode2.wasMode3 = 0;
        int mode = moveMode2.mode;
        if (mode != unit->moveMode2) {
            unit->moveMode2 = mode;
            UnitMarkChanged(unit, nullptr, 6);
        }
    }
    if (moveMode.wasMode3 != 0) {
        if (moveMode2.wasMode3 != 0 && MapCheapCell(static_cast<char*>(world) + 0x170, nullptr, unit) && running == 0) {
            At<int>(this, 0x178) = 0;
            return;
        }
        int& waited = At<int>(this, 0x178);
        ++waited;
        if (waited <= 5 && moveMode2.wasMode3 != 0) return;
        moveMode.wasMode3 = 0;
        int mode = moveMode.mode;
        if (mode != unit->moveMode) {
            unit->moveMode = mode;
            UnitMarkChanged(unit, nullptr, 6);
        }
        UnitUpdateSizeClass(unit);
        At<std::uint8_t>(this, 0x146) = 1;
        return;
    }
    int allowed = unit->player ? At<std::uint8_t>(unit->player, 0x108) : 1;
    if (moveMode.mode != kModeRun || moveMode2.wasMode3 == 0) return;
    if (allowed == 0 && !(At<float>(ServerStatsOf(unit), 0x14) > 0.0f)) return;
    if (!MapCheapCell(static_cast<char*>(world) + 0x170, nullptr, unit)) return;
    if (running != 0) return;
    At<int>(this, 0x178) = 0;
    moveMode.wasMode3 = 1;
    int mode = UnitModeOf(moveMode);
    if (mode != unit->moveMode) {
        unit->moveMode = mode;
        UnitMarkChanged(unit, nullptr, 6);
    }
    UnitUpdateSizeClass(unit);
    At<std::uint8_t>(this, 0x146) = 1;
}
EI_REPLACE_METHOD(0x00548520, CUnitServer::UpdateSneak);

bool CUnitServer::ChangeMoveMode() {
    if (moveMode.mode == moveMode2.mode && moveMode.flag4 == moveMode2.flag4) return false;
    bool modeChanged = moveMode.mode != moveMode2.mode;
    MoveModeState saved = moveMode;
    SectorRemove(kSectorMap, nullptr, unit);
    bool passableBefore = MapCellPassable(kAIMap, nullptr, unit);
    bool sameMode = moveMode.mode == moveMode2.mode;
    moveMode = moveMode2;
    unit->moveMode = UnitModeOf(moveMode);
    UnitUpdateSizeClass(unit);
    bool placed;
    if (!sameMode && !(passableBefore ? MapCellPassable(kAIMap, nullptr, unit) : MapCanLeaveCell(kAIMap, nullptr, unit)))
        placed = false;
    else
        placed = SectorAdd(kSectorMap, nullptr, unit);
    UnitMarkChanged(unit, nullptr, 6);
    if (!placed) {
        // The new mode does not fit here: back to the old one, and the party is told.
        moveMode = saved;
        unit->moveMode = UnitModeOf(moveMode);
        moveMode2.mode = moveMode.mode;
        if (moveMode.mode != 2) moveMode2.wasMode3 = 0;
        unit->moveMode2 = UnitModeOf(moveMode2);
        UnitUpdateSizeClass(unit);
        SectorAdd(kSectorMap, nullptr, unit);
        CUnit* self = unit;
        if (self->player) PlayerMessage(At<void*>(self->player, 0x20), nullptr, self, kMessageModeBlocked);
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
            CtlPlayAnimation(this, nullptr, copy);
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
    ContextOf(this);
    CtlUpdateSneak(this);
    At<std::uint8_t>(this, 0x146) = 0;
    At<std::uint8_t>(this, 0x145) = 0;
    PathClear(Motion(this));
    std::uint32_t keep = At<std::uint32_t>(unit, 0x43C);
    void* world = ContextOf(this);
    At<std::uint8_t>(this, 0xAA) = unit->player ? 0 : 1;   // the path's +0x6E
    std::uint32_t before = At<std::uint32_t>(unit, 0x43C);
    At<std::uint32_t>(unit, 0x43C) = keep;
    bool found = MapFindPath(static_cast<char*>(world) + 0x170, nullptr, unit, point, Motion(this), range, 0.0f);
    At<std::uint32_t>(unit, 0x43C) = before;
    if (!found) {
        std::uint8_t blocked = At<std::uint8_t>(this, 0xA9);
        At<std::uint8_t>(this, 0x147) = 0;
        CtlStandStill(this);
        CtlSetState(this, nullptr, kStateChasing);
        At<std::uint8_t>(this, 0x145) = blocked == 1 ? 1 : 0;
        return false;
    }
    At<float>(this, 0xA4) = range;
    At<int>(this, 0x178) = 0;
    UnitPathSpeeds(unit);
    if (state < 0 || (state > 1 && state != kStateTurning)) CtlPlaySteps(this, nullptr, CtlIdleAnimation(this));
    state = kStateIdle;
    UnitUpdateSizeClass(unit);
    DropEffectObject(this);
    if (At<std::uint8_t>(this, 0x147) != 0) {
        // Moving already: the movement animation goes on unless it is the same one-step loop.
        AnimationSteps* next = CtlNextAnimation(this);
        AnimationSteps moving;
        StepsCopyConstruct(&moving, nullptr, next);
        moving.extra = next->extra;
        AnimationSteps current;
        StepsConstruct(&current);
        current.vtable = kAnimationStepsVtable;
        StepsCopy(&current, nullptr, &Queue(this));
        current.extra = QueueTime(this);
        if (current.count != 1 || moving.count != 1 || current.steps[0].unknown_01 != moving.steps[0].unknown_01) {
            StepsCopy(&Queue(this), nullptr, &moving);
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
    void* world = ContextOf(this);
    char* player = unit->player;
    // RemoveAll.
    for (UnitList::Node* node = list->head; node; node = node->next) {
    }
    list->count = 0;
    list->free = nullptr;
    list->tail = nullptr;
    list->head = nullptr;
    PlexFreeDataChain(list->blocks);
    list->blocks = nullptr;
    if (!player || At<int>(player, 0x100) != 1) return;
    // Away from the point: the units near it that are not in the player's group (+0x510C of its
    // messages object) are lifted off the sector map and stay in the list.
    float dx = point[0] - unit->positionX;
    float dy = point[1] - unit->positionY;
    float dz = point[2] - unit->positionZ;
    if ((dz * dz + dy * dy) + dx * dx > *reinterpret_cast<const float*>(0x0073CA98)) {
        void* sectors = static_cast<char*>(world) + 0x294;
        SectorFind(sectors, nullptr, point, list, 4.0f);
        void* group = At<char*>(player, 0x20) + 0x510C;
        for (UnitList::Node* node = list->head; node;) {
            UnitList::Node* current = node;
            node = node->next;
            CEngineObject* other = current->data;
            if (GroupContains(group, nullptr, other) == 0) {
                SectorRemove(sectors, nullptr, other);
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
    const auto* party = reinterpret_cast<const PartyMap*>(player + 0x40);
    for (PartyNode* node = FirstNode(party); node; node = NextNode(party, node)) {
        CUnit* member = node->unit;
        if (!member) break;
        if (member == unit) continue;
        auto* controller = reinterpret_cast<CUnitServer*>(member->controller);
        bool lift = moving && (controller->action.kind == kOrderMove || controller->action2.kind == kOrderMove);
        if (!lift) lift = BrainLeader(controller->brain) == unit;
        if (!lift) continue;
        auto* added = static_cast<UnitList::Node*>(ListNewNode(list, nullptr, list->tail, nullptr));
        added->data = member;
        if (list->tail) list->tail->next = added;
        else list->head = added;
        list->tail = added;
        SectorRemove(kSectorMap, nullptr, member);
    }
}
EI_REPLACE_METHOD(0x005468D0, CUnitServer::LiftNearby);

void CUnitServer::ReportPath(void* path, char partial) {
    if (At<int>(brain, 4) == 6) return;
    CUnit* self = unit;
    const float* goal = &At<float>(path, 0x60);
    // The original passes this point uninitialised when the path is not partial (then unused).
    float end[3] = {0.0f, 0.0f, 0.0f};
    int message;
    if (!partial) {
        if (self->player) PlayerPathMessage(At<void*>(self->player, 0x20), nullptr, self, end, goal, 3, 1, kFarBits);
        if (BrainLeader(brain)) return;
        self = unit;
        if (!self->player) return;
        message = kMessageCannotDo;
    } else {
        PathEndPoint(path, nullptr, end);
        float dy = self->positionY - goal[1];
        float dx = self->positionX - goal[0];
        float reached = dx * dx + dy * dy;
        float half = HalfCell();
        float ey = half * At<float>(path, 0x5C) - goal[1];
        float ex = half * At<float>(path, 0x58) - goal[0];
        float left = ex * ex + ey * ey;
        bool far = reached * *reinterpret_cast<const float*>(0x0073F0B8) > left;   // unordered: not far
        if (self->player) PlayerPathMessage(At<void*>(self->player, 0x20), nullptr, self, end, goal, 0, far ? 0 : 1, kFarBits);
        if (BrainLeader(brain)) return;
        self = unit;
        if (!self->player) return;
        message = far ? kMessagePartial : kMessageCannotDo;
    }
    PlayerMessage(At<void*>(self->player, 0x20), nullptr, self, message);
}
EI_REPLACE_METHOD(0x0054EB30, CUnitServer::ReportPath);

// --- Reset ---------------------------------------------------------------------------------------

void CUnitServer::ResetController() {
    auto releaseRef = [](CEngineObject*& ref) {   // 0052B030
        if (ref) ei::VCall<void>(ref, kSlotRelease);
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
    At<float>(this, 0x130) = 1.0f;
    StepsSetSize(&At<AnimationSteps>(this, 0x148), nullptr, 0, -1);
    At<int>(this, 0x15C) = -1;
    state = kStateChasing;
    hitDirection = 0xFFFFFFFFu;
    keepHitDirection = 0;
    At<std::uint8_t>(this, 0x145) = 0;
    At<std::uint8_t>(this, 0x146) = 0;
    At<std::uint8_t>(this, 0x147) = 0;
    moveMode.mode = 2;
    moveMode.flag4 = 0;
    moveMode.wasMode3 = 0;
    moveMode2.mode = 2;
    moveMode2.flag4 = 0;
    moveMode2.wasMode3 = 0;
    CEngineObject* effect = effectObject;
    effectObject = nullptr;
    if (effect) ei::VCall<void>(effect, kSlotRelease);
    At<int>(this, 0x180) = -1;
    action.kind = 0;
    action2.kind = kOrderNone;
    At<int>(this, 0x184) = 0;
    At<int>(this, 0x188) = 0;
    At<std::uint8_t>(this, 0x18C) = 0;
    At<std::uint8_t>(this, 0x18D) = 0;
    At<std::uint8_t>(this, 0x1D8) = 0;
    CEngineObject*& waitingFor = At<CEngineObject*>(this, 0x1DC);
    CEngineObject* waited = waitingFor;
    waitingFor = nullptr;
    if (waited) ei::VCall<void>(waited, kSlotRelease);
    At<std::uint8_t>(this, 0x18E) = 0;
    BrainResetAll(brain);
    AlarmReset(reinterpret_cast<char*>(this) + 0xB10);
    At<void*>(this, 0x918) = nullptr;
    moveFactors[0] = 1.0f;
    moveFactors[2] = 1.0f;
    moveFactors[3] = 1.0f;
    moveFactors[1] = 1.0f;
    StatsReset(&stats);
    At<int>(this, 0x908) = 0;
    At<int>(this, 0x90C) = -1;
    At<int>(this, 0x910) = -1;
    speedClass = 0xF;
    claimedHostile = 0;
    At<float>(this, 0x8F4) = 0.0f;
    MoveKey(this) = 0x666DEAD;
    StepsSetSize(&MoveSteps(this), nullptr, 0, -1);
    MoveSteps(this).extra = -1;
    IdleKey(this) = 0x666DEAD;
    StepsSetSize(&IdleSteps(this), nullptr, 0, -1);
    IdleSteps(this).extra = -1;
    At<int>(this, 0x938) = 0;
    // The armour spell list (+0x920, a CList of {item ref, countdown, ?}) emptied: each element's
    // reference released (00552B80), then RemoveAll.
    struct SpellNode {
        SpellNode* next;
        SpellNode* prev;
        CEngineObject* item;
        int countdown;
        int unknown_10;
    };
    for (auto* node = At<SpellNode*>(this, 0x920); node; node = node->next) {
        if (node->item) ei::VCall<void>(node->item, kSlotRelease);
    }
    At<int>(this, 0x928) = 0;
    At<int>(this, 0x92C) = 0;
    At<int>(this, 0x924) = 0;
    At<int>(this, 0x920) = 0;
    PlexFreeDataChain(At<void*>(this, 0x930));
    At<int>(this, 0x930) = 0;
    At<int>(this, 0x978) = 0;
}
EI_REPLACE_METHOD(0x00546480, CUnitServer::ResetController);
