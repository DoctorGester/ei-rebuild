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

// What CUnitServer::PlanAction (combat_sight.cpp) saw on its last run, for the verify build's
// mismatch reports. Written on every run; nothing in the game reads it.
struct PlanTrace {
    int now;
    int duration;
    int ticks;
    int castDelay;
    float heading;
    float turnSpeed;
    float reachSq;
    float outerSq;
    std::uint8_t predict;
    struct Tick {
        float distanceSq;   // the stored (float) squared distance
        float turn;         // the turn angle (0 when not computed)
        int turnTicks;      // _ftol(angle / turnSpeed), -1 when not computed
        std::uint8_t path;  // 0 waited (closer), 1 gave up, 2 tried, 3 waited (turning), 4 refused
    } at[64];
};
inline PlanTrace& LastPlanTrace() {
    static PlanTrace trace;
    return trace;
}
