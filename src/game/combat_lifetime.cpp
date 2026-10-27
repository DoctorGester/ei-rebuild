// The server unit controller's constructor, its AI brain's constructor and destructor and the perception
// memory's destructor, and the plan taking a step list. See docs/notes/combat.md.
#include "combat.h"
#include "combat_plan.h"
#include "core_refcount.h"
#include "record_arrays.h"
#include "unit_ai.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "game_data.h"
#include "game/game_slots.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
constexpr std::uint32_t kNoAnimationKey = 0x666DEAD;
constexpr float kNoRange = std::bit_cast<float>(0x7E967699u);   // 1e38
constexpr int kBlockSize = 10;


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
void InitObjectList(mfc::CTypedPtrList<CUnit>& list) {
    list = {};
    list.vtable = gameslots::VictimListClass::Vtable();
    list.blockSize = kBlockSize;
}

// Its destructor: the vtable, each element released and cleared, then RemoveAll.
void DestroyObjectList(mfc::CTypedPtrList<CUnit>& list) {
    list.vtable = gameslots::VictimListClass::Vtable();
    for (auto* node = list.head; node; node = node->next) {
        if (node->data) node->data->Release();
        node->data = nullptr;
    }
    list.RemoveAll();
}

// The attack option arrays' destructor (inlined for the second): the vtable, the data freed (the options have
// no destructor).
void DestroyAttackOptions(AIAttackOptions& options) {
    options.vtable = mfc::impl::AttackOptionArrayClass::Vtable();
    if (options.data) gamecrt::operator_delete(options.data);
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
    armourSpells.vtable = gameslots::ArmourSpellListClass::Vtable();
    armourSpells.blockSize = kBlockSize;
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
EI_REWRITTEN_SPECIAL(0x00545580, CUnitServer::CUnitServer);

// --- AIPath -------------------------------------------------------------------------------------

void AIPath::Destruct() {
    excludedBlocks.vtable = mfc::impl::PointArrayClass::Vtable();
    if (excludedBlocks.data) gamecrt::operator_delete(excludedBlocks.data);
    cells.vtable = PathCellListVtable();
    cells.RemoveAll();
    steps.vtable = PathCellListVtable();
    steps.RemoveAll();
}
EI_REWRITTEN(0x00546100, AIPath::Destruct);

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
EI_REWRITTEN(0x00545110, OrderPlan::TakeSteps);

// --- AIBrain ------------------------------------------------------------------------------------

AIBrain* AIBrain::Construct() {
    LooksInit(&move.looks);
    move.looks.vtable = mfc::impl::AiLookRecordArrayClass::Vtable();
    move.Clear();
    target = nullptr;
    seen = nullptr;
    modes = {};
    modes.vtable = gameslots::OwnedRefListClass::Vtable();
    modes.blockSize = kBlockSize;
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
    memory.calls.vtable = mfc::impl::MemoryRecordArrayClass::Vtable();   // the same virtuals
    Init();
    return this;
}
EI_REWRITTEN(0x00545990, AIBrain::Construct);

void AIMemory::Destruct() {
    calls.vtable = mfc::impl::MemoryRecordArrayClass::Vtable();
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
EI_REWRITTEN(0x00545CB0, AIMemory::Destruct);

void AIBrain::Destruct() {
    Init();
    memory.Destruct();
    DestroyAttackOptions(attention.spells);
    DestroyAttackOptions(attention.weapons);
    alarmRecords.vtable = recordarrays::BehaviourArrayClass::Vtable();
    if (alarmRecords.data) {
        AIAlarmRecordElements::Destruct(alarmRecords.data, alarmRecords.size);
        gamecrt::operator_delete(alarmRecords.data);
    }
    if (current) current->Release();
    modes.vtable = gameslots::OwnedRefListClass::Vtable();   // the destructor writes the same vtable
    for (auto* node = modes.head; node; node = node->next) DestroyOwned(node->data);
    modes.RemoveAll();
    if (seen) seen->Release();
    seen = nullptr;
    if (target) target->Release();
    target = nullptr;
    move.looks.vtable = mfc::impl::AiLookArrayClass::Vtable();
    if (move.looks.data) gamecrt::operator_delete(move.looks.data);
}
EI_REWRITTEN(0x005457E0, AIBrain::Destruct);
