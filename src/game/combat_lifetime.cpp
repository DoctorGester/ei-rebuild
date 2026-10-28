// The server unit controller's constructor, its AI brain's constructor and destructor and the perception
// memory's destructor, and the plan taking a step list. See docs/notes/combat.md.
#include "combat.h"
#include "combat_plan.h"
#include "core_refcount.h"
#include "unit_ai.h"

#include "lib/crt.h"
#include "game_data.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>

namespace {
constexpr std::uint32_t kNoAnimationKey = 0x666DEAD;
constexpr float kNoRange = std::bit_cast<float>(0x7E967699u);   // 1e38

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
    motion.Construct();
    effectObject = nullptr;
    InitAction(action);
    InitAction(action2);
    followedLeader = nullptr;
    followsLeader = 0;
    armourSpells = {};
    moveAnimationKey = kNoAnimationKey;
    idleKey = kNoAnimationKey;
    brain.Construct();
    std::memset(&voice, 0, sizeof(voice));
    ResetController();
}

// --- AIPath -------------------------------------------------------------------------------------

void AIPath::Destruct() {
    lib::FreeStorage(excludedBlocks);
    cells.clear();
    steps.clear();
}

// --- OrderPlan ----------------------------------------------------------------------------------

void OrderPlan::TakeSteps(AnimationSteps steps, int start, int duration) {
    targetId = -1;
    startTime = start;
    hitLocation = 1;
    rangeSq = kNoRange;
    isCast = 0;
    notSkill = 0;
    castKind = 8;
    endTime = start + duration;
    animation = std::move(steps);
}

// --- AIBrain ------------------------------------------------------------------------------------

AIBrain* AIBrain::Construct() {
    move.Clear();
    target = nullptr;
    seen = nullptr;
    modes = {};
    current = nullptr;
    alarmRecords = {};
    attention.weapons = {};
    attention.spells = {};
    // The perception memory (inlined): two sight caches, four object lists, reports and calls.
    memory.spareCells = kEmptySightCache;
    memory.sightCells = kEmptySightCache;
    InitObjectList(memory.nearby);
    InitObjectList(memory.visible);
    InitObjectList(memory.seenCorpses);
    InitObjectList(memory.corpses);
    memory.reports = {};
    memory.calls = {};
    Init();
    return this;
}

void AIMemory::Destruct() {
    calls.clear();
    reports.clear();   // each reference released
    DestroyObjectList(corpses);
    DestroyObjectList(seenCorpses);
    DestroyObjectList(visible);
    DestroyObjectList(nearby);
}
