// The server unit controller's constructor (00545580), its AI brain's constructor and destructor
// (00545990, 005457E0) and the perception memory's destructor (00545CB0), and the plan taking a
// step list (00545110). A-U7; see docs/notes/combat.md.
#include "combat.h"
#include "combat_plan.h"
#include "core_refcount.h"
#include "originals_library.h"
#include "originals_units.h"
#include "unit_ai.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
// The vtables the constructors and destructors install.
constexpr std::uintptr_t kControllerVtable = std::uintptr_t(0x0073EDBC);   // the abstract controller
constexpr std::uintptr_t kUnitServerVtable = std::uintptr_t(0x0073EF38);
void* const kArmourSpellListVtable = reinterpret_cast<void*>(0x0073F010);
void* const kModeListVtable = reinterpret_cast<void*>(0x0073F028);
void* const kAlarmRecordsVtable = reinterpret_cast<void*>(0x0073F04C);
void* const kAlarmRecordsBaseVtable = reinterpret_cast<void*>(0x0073F034);
void* const kAttackOptionsVtable = reinterpret_cast<void*>(0x0073F040);
void* const kObjectListVtable = reinterpret_cast<void*>(0x0073E730);
void* const kReportsVtable = reinterpret_cast<void*>(0x0073F140);
void* const kReportsBaseVtable = reinterpret_cast<void*>(0x0073F058);
void* const kCallsVtable = reinterpret_cast<void*>(0x0073F070);
void* const kCallsBaseVtable = reinterpret_cast<void*>(0x0073F064);

constexpr std::uint32_t kNoAnimationKey = 0x666DEAD;
constexpr float kNoRange = std::bit_cast<float>(0x7E967699u);   // 1e38
constexpr int kBlockSize = 10;

static_assert(offsetof(AIBrain, looks) == offsetof(AIBrain, actionPoint) + offsetof(AIMoveOrder, looks));

// A step list's constructor (00556850 / 00546060, and 00557B60 with the derived vtable written
// after it): no steps; `extra` is left as it was.
void InitSteps(AnimationSteps& steps) {
    steps.vtable = kAnimationStepsVtable;
    steps.steps = nullptr;
    steps.count = 0;
    steps.capacity = 0;
    steps.growBy = 0;
}

// An order's constructor (00545F70: kind 9, no target, flag18 clear), then its Clear (00545F40)
// and the rest of its fields.
void InitAction(UnitAction& action) {
    action.target = nullptr;
    action.kind = 9;
    action.flag18 = 0;
    ActionClear(&action);
    action.unknown_1C = 0;
    action.unknown_20 = 0;
}

// The empty CList of counted object references the memory keeps (00556C80 with blocks of 10).
void InitObjectList(mfc::CTypedPtrList<CEngineObject>& list) {
    list = {};
    list.vtable = kObjectListVtable;
    list.blockSize = kBlockSize;
}

// Its destructor: the vtable, each element released and cleared (0052CEC0 / 0052D030), then
// RemoveAll.
void DestroyObjectList(mfc::CTypedPtrList<CEngineObject>& list) {
    list.vtable = kObjectListVtable;
    for (auto* node = list.head; node; node = node->next) {
        if (node->data) node->data->Release();
        node->data = nullptr;
    }
    list.RemoveAll();
}

// The attack option arrays' destructor (00557410, inlined for the second): the vtable, the data
// freed (the options have no destructor).
void DestroyAttackOptions(AIAttackOptions& options) {
    options.vtable = kAttackOptionsVtable;
    if (options.data) gamecrt::operator_delete(options.data);
}

// The sight cache's empty state ({0, -1}).
constexpr SpiralCache kEmptySightCache{0, -1.0f};
} // namespace

// --- CUnitServer --------------------------------------------------------------------------------

CUnitServer* CUnitServer::Construct() {
    // CEngineObject's constructor (inlined).
    refCount_ = 0;
    id_ = -1;
    unknown_18 = 0;
    type_ = 0;
    flags_ = 0;
    owner_ = nullptr;
    ei::SetVtable(this, kControllerVtable);
    unit = nullptr;
    InitSteps(queue);
    motion.Construct();
    InitSteps(currentPlan.animation);
    InitSteps(savedQueue);
    effectObject = nullptr;
    InitAction(action);
    InitAction(action2);
    followedLeader = nullptr;
    followsLeader = 0;
    armourSpells = {};
    armourSpells.vtable = kArmourSpellListVtable;
    armourSpells.blockSize = kBlockSize;
    moveAnimationKey = kNoAnimationKey;
    InitSteps(moveAnimation);
    StepsSetSize(&moveAnimation, nullptr, 0, -1);
    moveAnimation.extra = -1;
    // 00545FF0: the idle key and list (its SetSize(0) inlined on the empty list).
    idleKey = kNoAnimationKey;
    InitSteps(idleAnimation);
    idleAnimation.extra = -1;
    reinterpret_cast<AIBrain*>(brain)->Construct();
    std::memset(voice, 0, sizeof(voice));   // 005AEA50
    ei::SetVtable(this, kUnitServerVtable);
    ResetController();
    return this;
}
EI_REPLACE_METHOD(0x00545580, CUnitServer::Construct);

// --- AIPath -------------------------------------------------------------------------------------

void AIPath::Destruct() {
    excludedBlocks.vtable = reinterpret_cast<void*>(kExcludedBlocksVtable);
    if (excludedBlocks.data) gamecrt::operator_delete(excludedBlocks.data);
    cells.vtable = PathCellListVtable();
    cells.RemoveAll();
    steps.vtable = PathCellListVtable();
    steps.RemoveAll();
}
EI_REPLACE_METHOD(0x00546100, AIPath::Destruct);

// --- OrderPlan ----------------------------------------------------------------------------------

void OrderPlan::TakeSteps(AnimationSteps steps, int start, int duration) {
    StepsSetSize(&animation, nullptr, 0, -1);
    animation.extra = -1;
    targetId = -1;
    startTime = start;
    unknown_2C = 1;
    rangeSq = kNoRange;
    isCast = 0;
    notSkill = 0;
    castKind = 8;
    endTime = start + duration;
    StepsSetSize(&animation, nullptr, steps.count, -1);
    for (int i = 0; i < steps.count; ++i) animation.steps[i] = steps.steps[i];
    animation.extra = steps.extra;
    isCast = 0;
    // The by-value list's destructor: only its data is freed.
    if (steps.steps) gamecrt::operator_delete(steps.steps);
}
EI_REPLACE_METHOD(0x00545110, OrderPlan::TakeSteps);

// --- AIBrain ------------------------------------------------------------------------------------

AIBrain* AIBrain::Construct() {
    LooksInit(&looks);
    looks.vtable = reinterpret_cast<void*>(kLookArrayVtable);
    MoveOrder()->Clear();
    target = nullptr;
    seen = nullptr;
    modes = {};
    modes.vtable = kModeListVtable;
    modes.blockSize = kBlockSize;
    current = nullptr;
    alarmRecords = {};
    alarmRecords.vtable = kAlarmRecordsVtable;
    attention.weapons = {};
    attention.weapons.vtable = kAttackOptionsVtable;
    attention.spells = {};
    attention.spells.vtable = kAttackOptionsVtable;
    // The perception memory (inlined): two sight caches, four object lists, reports and calls.
    std::memcpy(memory, &kEmptySightCache, sizeof(kEmptySightCache));
    sightCells = kEmptySightCache;
    InitObjectList(unknown_0F4);
    InitObjectList(visible);
    InitObjectList(unknown_12C);
    InitObjectList(corpses);
    reports = {};
    reports.vtable = kReportsVtable;
    calls = {};
    calls.vtable = kCallsVtable;
    Init();
    return this;
}
EI_REPLACE_METHOD(0x00545990, AIBrain::Construct);

void AIBrain::DestructMemory() {
    calls.vtable = kCallsBaseVtable;
    if (calls.data) {
        for (int i = 0; i < calls.size; ++i) calls.data[i].Destruct();   // 00558CC0(0)
        gamecrt::operator_delete(calls.data);
    }
    reports.vtable = kReportsBaseVtable;
    if (reports.data) {
        for (int i = 0; i < reports.size; ++i) {   // 00558CA0(0): the reference released
            if (reports.data[i].object) reports.data[i].object->Release();
            reports.data[i].object = nullptr;
        }
        gamecrt::operator_delete(reports.data);
    }
    DestroyObjectList(corpses);
    DestroyObjectList(unknown_12C);
    DestroyObjectList(visible);
    DestroyObjectList(unknown_0F4);
}

namespace {
// The original's `this` is the memory at +0xD0.
void __fastcall DestructMemoryHook(std::uint8_t* memory) {
    reinterpret_cast<AIBrain*>(memory - offsetof(AIBrain, memory))->DestructMemory();
}
} // namespace
EI_REPLACE(0x00545CB0, DestructMemoryHook);

void AIBrain::Destruct() {
    Init();
    DestructMemory();
    DestroyAttackOptions(attention.spells);
    DestroyAttackOptions(attention.weapons);
    alarmRecords.vtable = kAlarmRecordsBaseVtable;
    if (alarmRecords.data) {
        AIAlarmRecordElements::Destruct(alarmRecords.data, alarmRecords.size);   // 005588F0
        gamecrt::operator_delete(alarmRecords.data);
    }
    if (current) current->Release();
    modes.vtable = kModeListVtable;   // the destructor writes the same vtable
    for (auto* node = modes.head; node; node = node->next) reinterpret_cast<RefPtr*>(&node->data)->Destroy();
    modes.RemoveAll();
    if (seen) seen->Release();
    seen = nullptr;
    if (target) target->Release();
    target = nullptr;
    looks.vtable = reinterpret_cast<void*>(kLookArrayBaseVtable);
    if (looks.data) gamecrt::operator_delete(looks.data);
}
EI_REPLACE_METHOD(0x005457E0, AIBrain::Destruct);
