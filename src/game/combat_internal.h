#pragma once

// Helpers shared by the combat*.cpp files (not used elsewhere).

#include "game/combat.h"
#include "game/console.h"

class PlayerServer;

// The combat log: the AI stream.
inline constexpr console::Stream& kCombatLog = console::g_aiLogOut;

// Defined in combat_actions.cpp: the remark `kind` of `unit` to its player.
void PlayerMessage(PlayerServer* messages, Unit* unit, AckKind kind);

