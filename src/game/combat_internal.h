#pragma once

// Helpers shared by combat.cpp and combat_actions.cpp (not used elsewhere).

#include "combat.h"
#include "game_data.h"
#include "lib/crt_iostream.h"

// The game's (MSVC 6 <iostream.h>) ostream; only the inserters the combat logs use (combat.cpp).
struct GameOstream : libcrt::Ostream {
    GameOstream* Put(const char* text);
    GameOstream* Put(long value);
    GameOstream* Put(double value);
    GameOstream* PutChar(int c);
    GameOstream* Apply(libcrt::Manipulator manipulator);  // manipulator(*this)

    // The logs set x_floatused to 1 before each floating point number.
    void SetField4() { floatUsed = 1; }
};
static_assert(sizeof(GameOstream) == 8);

// The combat log (the AI stream; combat.cpp) and the manipulator that ends a line.
GameOstream* GameLog();
inline const libcrt::Manipulator kLogEndLine = libcrt::FlushManipulator;

// The unit's server object (CUnit + 0x170, a CMapObjectServer) and the stats block at its +0x20.
inline CEngineObject* ServerObjectOf(CUnit* unit) { return unit->companion; }
inline UnitVitals* ServerStatsOf(CUnit* unit) { return &unit->ServerCompanion()->vitals; }

// Defined in combat_actions.cpp.
// Player messages (player + 0x20 points at it): message `code` about `unit`.
void PlayerMessage(void* messages, CUnit* unit, int code);

