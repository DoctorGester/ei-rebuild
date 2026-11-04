#pragma once

// Helpers shared by combat.cpp and combat_actions.cpp (not used elsewhere).

#include "combat.h"
#include "console.h"

class PlayerServer;

// The combat log: the AI stream.
inline constexpr console::Stream& kCombatLog = console::g_aiLogOut;

// Defined in combat_actions.cpp.
// Player messages (player + 0x20 points at it): message `code` about `unit`.
void PlayerMessage(PlayerServer* messages, Unit* unit, AckKind kind);

