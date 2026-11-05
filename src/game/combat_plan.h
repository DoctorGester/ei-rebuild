#pragma once

// Structures of the order planning code (combat_sight.cpp, combat_tick.cpp): the animation lists
// the planner builds and the plan of an order.

#include "game/combat.h"

#include <cstddef>
#include <cstdint>

// AnimationStep, AnimationSteps and OrderPlan are declared in combat.h (UnitServer holds a plan).

// Where an order must be seen from.
struct SightTarget {
    Vector3 point;              // the point to see (a target unit's is filled in later)
    float minDistanceSq = 0.0f;  // closer than this needs no line of sight
    float eyeHeight = 0.0f;     // the viewer's eye height above the ground
    Unit* unit = nullptr;      // the target unit, or null for a point
};

