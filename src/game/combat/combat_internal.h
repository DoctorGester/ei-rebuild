#pragma once

// Helpers shared by the combat*.cpp files (not used elsewhere), and the structures of the order planning code
// (combat_sight.cpp, combat_tick.cpp).

#include "game/combat/combat.h"
#include "game/console/console.h"

#include <cstddef>
#include <cstdint>

class PlayerServer;

// The combat log: the AI stream.
inline constexpr console::Stream& kCombatLog = console::g_aiLogOut;

// Defined in combat_actions.cpp: the remark `kind` of `unit` to its player.
void PlayerMessage(PlayerServer* messages, Unit* unit, AckKind kind);

// AnimationStep, AnimationSteps and OrderPlan (the animation lists the planner builds and the plan of an order) are
// declared in combat.h (UnitServer holds a plan).

// Where an order must be seen from.
struct SightTarget {
    Vector3 point;              // the point to see (a target unit's is filled in later)
    float minDistanceSq = 0.0f;  // closer than this needs no line of sight
    float eyeHeight = 0.0f;     // the viewer's eye height above the ground
    Unit* unit = nullptr;      // the target unit, or null for a point
};
