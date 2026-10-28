// The server unit controller's constructor, its AI brain's constructor and destructor and the perception
// memory's destructor, and the plan taking a step list. See docs/notes/combat.md.
#include "combat.h"
#include "combat_plan.h"
#include "core_refcount.h"
#include "record_arrays.h"
#include "unit_ai.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "game_data.h"
#include "game/game_slots.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>

namespace {
constexpr std::uint32_t kNoAnimationKey = 0x666DEAD;
constexpr float kNoRange = std::bit_cast<float>(0x7E967699u);   // 1e38

// A step list's constructor (also with the derived vtable written after it): no steps; `extra` is left as it
// was.
void InitSteps(AnimationSteps& steps) {
    steps.vtable = mfc::impl::AnimationStepsCopyClass::Vtable();
    steps.data = nullptr;
    steps.size = 0;
    steps.maxSize = 0;
    steps.growBy = 0;
}

// An order's constructor (kind 9, no target, flag18 clear), then its Clear and the rest of its fields.
void InitAction(UnitAction& action) {
    action.target = nullptr;
    action.kind = 9;
    action.flag18 = 0;
    action.Clear();
    action.param = 0;
    action.modifier = 0;
}

// The empty CList of counted object references the memory keeps (blocks of 10).
void InitObjectList(std::list<CUnit*>& list) {
    list = {};
}

// Its destructor: the vtable, each element released and cleared, then RemoveAll.
void DestroyObjectList(std::list<CUnit*>& list) {
    for (auto node = list.begin(); node != list.end(); ++node) {
        if (*node) (*node)->Release();
        *node = nullptr;
    }
    list.clear();
}

constexpr SpiralCache kEmptySightCache{0, -1.0f};
} // namespace

// --- CUnitServer --------------------------------------------------------------------------------

CUnitServer::CUnitServer() {
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
    moveAnimationKey = kNoAnimationKey;
    InitSteps(moveAnimation);
    moveAnimation.SetSize(0, -1);
    moveAnimation.extra = -1;
    // The idle key and list (its SetSize(0) inlined on the empty list).
    idleKey = kNoAnimationKey;
    InitSteps(idleAnimation);
    idleAnimation.extra = -1;
    brain.Construct();
    std::memset(&voice, 0, sizeof(voice));
    ResetController();
}

// --- AIPath -------------------------------------------------------------------------------------

void AIPath::Destruct() {
    lib::FreeStorage(excludedBlocks);
    cells.vtable = PathCellListVtable();
    cells.RemoveAll();
    steps.vtable = PathCellListVtable();
    steps.RemoveAll();
}

// --- OrderPlan ----------------------------------------------------------------------------------

void OrderPlan::TakeSteps(AnimationSteps steps, int start, int duration) {
    animation.SetSize(0, -1);
    animation.extra = -1;
    targetId = -1;
    startTime = start;
    hitLocation = 1;
    rangeSq = kNoRange;
    isCast = 0;
    notSkill = 0;
    castKind = 8;
    endTime = start + duration;
    animation.SetSize(steps.size, -1);
    for (int i = 0; i < steps.size; ++i) animation.data[i] = steps.data[i];
    animation.extra = steps.extra;
    isCast = 0;
    if (steps.data) gamecrt::operator_delete(steps.data);
}

// --- AIBrain ------------------------------------------------------------------------------------

AIBrain* AIBrain::Construct() {
    LooksInit(&move.looks);
    move.looks.vtable = mfc::impl::AiLookRecordArrayClass::Vtable();
    move.Clear();
    target = nullptr;
    seen = nullptr;
    modes = {};
    current = nullptr;
    alarmRecords = {};
    alarmRecords.vtable = recordarrays::BehaviourArrayClass::Vtable();   // the same virtuals
    attention.weapons = {};
    attention.weapons.vtable = mfc::impl::AttackOptionArrayClass::Vtable();
    attention.spells = {};
    attention.spells.vtable = mfc::impl::AttackOptionArrayClass::Vtable();
    // The perception memory (inlined): two sight caches, four object lists, reports and calls.
    memory.spareCells = kEmptySightCache;
    memory.sightCells = kEmptySightCache;
    InitObjectList(memory.nearby);
    InitObjectList(memory.visible);
    InitObjectList(memory.seenCorpses);
    InitObjectList(memory.corpses);
    memory.reports = {};
    memory.reports.vtable = gameslots::MemoryObjectArrayClass::Vtable();   // the same virtuals
    memory.calls = {};
    memory.calls.vtable = MemoryRecordArrayClass::Vtable();   // the same virtuals
    Init();
    return this;
}

void AIMemory::Destruct() {
    calls.vtable = MemoryRecordArrayClass::Vtable();
    if (calls.data) {
        for (int i = 0; i < calls.size; ++i) calls.data[i].Destruct();   // the call records' destructors
        gamecrt::operator_delete(calls.data);
    }
    reports.vtable = gameslots::MemoryObjectArrayClass::Vtable();
    if (reports.data) {
        for (int i = 0; i < reports.size; ++i) {   // the reference released
            if (reports.data[i].object) reports.data[i].object->Release();
            reports.data[i].object = nullptr;
        }
        gamecrt::operator_delete(reports.data);
    }
    DestroyObjectList(corpses);
    DestroyObjectList(seenCorpses);
    DestroyObjectList(visible);
    DestroyObjectList(nearby);
}
