#pragma once

// Structures of the order planning code (combat_sight.cpp, combat_tick.cpp): the animation lists
// the planner builds and the plan of an order. Names provisional.

#include "combat.h"

#include <cstddef>
#include <cstdint>

// AnimationStep, AnimationSteps and OrderPlan are declared in combat.h (CUnitServer holds a plan).

// Where an order must be seen from (0x18 bytes).
struct SightTarget {
    Vector3 point;              // the point to see (a target unit's is filled in later)
    float minDistanceSq;        // closer than this needs no line of sight
    float eyeHeight;            // the viewer's eye height above the ground
    CUnit* unit;                // the target unit, or null for a point
};
static_assert(sizeof(SightTarget) == 0x18);

