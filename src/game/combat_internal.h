#pragma once

// Helpers shared by combat.cpp and combat_actions.cpp (not used elsewhere).

#include "combat.h"
#include "game_data.h"
#include "lib/crt_iostream.h"

// The game's (MSVC 6 <iostream.h>) ostream; only the inserters the combat logs use (combat.cpp).
struct GameOstream : libcrt::Ostream {
    GameOstream* Put(const char* text);     // 006FC31D
    GameOstream* Put(long value);           // 006FC9C6
    GameOstream* Put(double value);         // 006FC7FA
    GameOstream* PutChar(int c);            // 006FC0B0
    GameOstream* Apply(libcrt::Manipulator manipulator);  // 00434FE0: manipulator(*this)

    // The logs set x_floatused to 1 before each floating point number.
    void SetField4() { floatUsed = 1; }
};
static_assert(sizeof(GameOstream) == 8);

// The game's log stream (global 007AF3E8) and the manipulator that ends a line (00435000).
inline GameOstream* GameLog() { return ei::GameData<GameOstream*>(0x007AF3E8); }
inline const libcrt::Manipulator kLogEndLine = libcrt::FlushManipulator;

// The unit's server object (CUnit + 0x170, a CMapObjectServer) and the stats block at its +0x20.
inline CEngineObject* ServerObjectOf(CUnit* unit) { return unit->companion; }
inline UnitVitals* ServerStatsOf(CUnit* unit) { return &static_cast<CMapObjectServer*>(ServerObjectOf(unit))->vitals; }

// Forwarders defined in combat_actions.cpp (__fastcall with the unused EDX for __thiscall).
// 0066D6E0. Player messages (player + 0x20 points at it): message `code` about `unit`.
void __fastcall PlayerMessage(void* messages, void*, CUnit* unit, int code);
// 005C98C0 (world + 0x2838, the AI events): a noise of `loudness` and `kind` made by `source`.

// 005D7E90: the AI's memory of attackers (CUnitServer + 0x980) remembers `attacker`: AIBrain::SetSeen.
