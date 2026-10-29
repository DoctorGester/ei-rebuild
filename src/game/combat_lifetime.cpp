// The server unit controller's constructor and the plan taking a step list. See docs/notes/combat.md.
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
constexpr float kNoRange = 1e38f;
static_assert(std::bit_cast<std::uint32_t>(kNoRange) == 0x7E967699u);
} // namespace

// --- UnitServer --------------------------------------------------------------------------------

UnitServer::UnitServer() { ResetController(); }

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
