// The server unit controller's constructor, its AI brain's constructor and destructor and the perception
// memory's destructor, and the plan taking a step list. See docs/notes/combat.md.
#include "combat.h"
#include "combat_plan.h"
#include "core_refcount.h"
#include "unit_ai.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>

namespace {
constexpr std::uint32_t kNoAnimationKey = 0x666DEAD;
constexpr float kNoRange = 1e38f;
static_assert(std::bit_cast<std::uint32_t>(kNoRange) == 0x7E967699u);

// An order's constructor (kind 9, no target, pointIsUseSpot clear), then its Clear and the rest of its fields.
void InitAction(UnitAction& action) {
    action.kind = UnitOrderKind::None;
    action.pointIsUseSpot = 0;
    action.Clear();
    action.param = 0;
    action.modifier = 0;
}


constexpr SpiralCache kEmptySightCache{0, -1.0f};
} // namespace

// --- CUnitServer --------------------------------------------------------------------------------

CUnitServer::CUnitServer() {
    motion.Construct();
    InitAction(action);
    InitAction(action2);
    followsLeader = 0;
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
    modes = {};
    alarmRecords = {};
    attention.weapons = {};
    attention.spells = {};
    // The perception memory (inlined): two sight caches, four object lists, reports and calls.
    memory.spareCells = kEmptySightCache;
    memory.sightCells = kEmptySightCache;
    memory.nearby.clear();
    memory.visible.clear();
    memory.seenCorpses.clear();
    memory.corpses.clear();
    memory.reports = {};
    memory.calls = {};
    Init();
    return this;
}

void AIMemory::Destruct() {
    calls.clear();
    reports.clear();   // each reference released
    corpses.clear();
    seenCorpses.clear();
    visible.clear();
    nearby.clear();
}
