#pragma once

// Helpers shared by combat.cpp and combat_actions.cpp (not used elsewhere).

#include "combat.h"
#include "console.h"

class CPlayerServer;

// The combat log (the AI stream; combat.cpp).
console::Stream& GameLog();

// The unit's server object (CUnit + 0x170, a CMapObjectServer) and the stats block at its +0x20.
inline CEngineObject* ServerObjectOf(CUnit* unit) { return unit->companion; }
inline UnitVitals* ServerStatsOf(CUnit* unit) { return &unit->ServerCompanion()->vitals; }

// Defined in combat_actions.cpp.
// Player messages (player + 0x20 points at it): message `code` about `unit`.
void PlayerMessage(CPlayerServer* messages, CUnit* unit, int code);

