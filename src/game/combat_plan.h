#pragma once

// Structures of the order planning code (combat_sight.cpp, combat_tick.cpp): the animation lists
// the planner builds and the plan of an order. Names provisional.

#include "combat.h"

#include <cstddef>
#include <cstdint>

// AnimationStep, AnimationSteps and OrderPlan are declared in combat.h (CUnitServer holds a plan).

inline void* const kAnimationStepsVtable = reinterpret_cast<void*>(0x0073F01C);
inline void* const kAnimationStepsBaseVtable = reinterpret_cast<void*>(0x0073EF2C);

// Where an order must be seen from (0x18 bytes; 0054BCB0 fills it).
struct SightTarget {
    Vector3 point;              // +0x00 the point to see (a target unit's is filled in later)
    float minDistanceSq;        // +0x0C closer than this needs no line of sight
    float eyeHeight;            // +0x10 the viewer's eye height above the ground
    CUnit* unit;                // +0x14 the target unit, or null for a point
};
static_assert(sizeof(SightTarget) == 0x18);

