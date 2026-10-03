#pragma once

// Structures of the order planning code (combat_sight.cpp, combat_tick.cpp): the animation lists
// the planner builds and the plan of an order. Names provisional.

#include "combat.h"

#include <cstddef>
#include <cstdint>

// One step of an animation list (0xC bytes; built by 0054F970 / 00550100 / 00550620).
struct AnimationStep {
    std::uint32_t data[3];
};
static_assert(sizeof(AnimationStep) == 0xC);

// An animation list: an MFC CArray of AnimationStep (vtable 0073F01C over the base 0073EF2C;
// 00557B80 is its SetSize) and a dword after it.
struct AnimationSteps {
    void* vtable;
    AnimationStep* steps;   // +0x04
    int count;              // +0x08
    int capacity;           // +0x0C
    int growBy;             // +0x10
    int extra;              // +0x14 copied along with the steps
};
static_assert(sizeof(AnimationSteps) == 0x18);

inline void* const kAnimationStepsVtable = reinterpret_cast<void*>(0x0073F01C);
inline void* const kAnimationStepsBaseVtable = reinterpret_cast<void*>(0x0073EF2C);

// The plan of an order (0x44 bytes; CUnitServer + 0xC4 holds the current one): its animation, when
// it starts and ends, and what it aims at. 0054CA40 fills it.
struct OrderPlan {
    AnimationSteps animation;   // +0x00
    int startTime;              // +0x18 game time
    int endTime;                // +0x1C startTime + the animation's duration
    int targetId;               // +0x20 -1: none
    float rangeSq;              // +0x24 squared distance the target may move away
    std::uint8_t isCast;        // +0x28
    std::uint8_t unknown_29[3];
    int unknown_2C;             // +0x2C 1
    std::uint8_t notSkill;      // +0x30 order source != 6
    std::uint8_t unknown_31[3];
    int castKind;               // +0x34 8; for a cast: 1 (spell kind 0x28), 0 (0x29) or the
                                //       spell prototype's +0x14
    float direction[3];         // +0x38 from the unit to the target
};
static_assert(offsetof(OrderPlan, startTime) == 0x18);
static_assert(offsetof(OrderPlan, direction) == 0x38);
static_assert(sizeof(OrderPlan) == 0x44);

// Where an order must be seen from (0x18 bytes; 0054BCB0 fills it).
struct SightTarget {
    float point[3];             // +0x00 the point to see (a target unit's is filled in later)
    float minDistanceSq;        // +0x0C closer than this needs no line of sight
    float eyeHeight;            // +0x10 the viewer's eye height above the ground
    CUnit* unit;                // +0x14 the target unit, or null for a point
};
static_assert(sizeof(SightTarget) == 0x18);
