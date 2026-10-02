#pragma once

// Helpers shared by combat.cpp and combat_actions.cpp (not used elsewhere).

#include "combat.h"

// The game's (MSVC 6 <iostream.h>) ostream; only the inserters the combat logs use (combat.cpp).
struct GameOstream {
    GameOstream* Put(const char* text);     // 006FC31D
    GameOstream* Put(long value);           // 006FC9C6
    GameOstream* Put(double value);         // 006FC7FA
    GameOstream* PutChar(int c);            // 006FC0B0
    GameOstream* Apply(void* manipulator);  // 00434FE0: manipulator(*this)

    // The logs set the dword at +4 to 1 before each floating point number.
    void SetField4() { *reinterpret_cast<int*>(reinterpret_cast<char*>(this) + 4) = 1; }
};

// The game's log stream (global 007AF3E8) and the manipulator that ends a line (00435000).
inline GameOstream* GameLog() { return *reinterpret_cast<GameOstream**>(0x007AF3E8); }
inline void* const kLogEndLine = reinterpret_cast<void*>(0x00435000);

// The unit's server object (CUnit + 0x170, a CMapObjectServer) and the stats block at its +0x20.
inline CEngineObject* ServerObjectOf(CUnit* unit) {
    return *reinterpret_cast<CEngineObject**>(reinterpret_cast<char*>(unit) + 0x170);
}
inline void* ServerStatsOf(CUnit* unit) { return reinterpret_cast<char*>(ServerObjectOf(unit)) + 0x20; }

// Forwarders defined in combat_actions.cpp (__fastcall with the unused EDX for __thiscall).
// 0058E850. CWorldServer: casts `spell` from `caster` at `from` on `target` at `at`.
void __fastcall WorldCastSpell(void* world, void*, void* spell, CUnit* caster, const float* from,
                               CEngineObject* target, const void* at, int);
// 0066D6E0. Player messages (player + 0x20 points at it): message `code` about `unit`.
void __fastcall PlayerMessage(void* messages, void*, CUnit* unit, int code);
// 005C98C0 (world + 0x2838, the AI events): a noise of `loudness` and `kind` made by `source`.
void __fastcall AIEventsNoise(void* events, void*, CUnitServer* source, float loudness, int kind, int);

// Forwarders defined in combat.cpp.
// 00524560. Recomputes the unit's server stats (hit points and the like) after a change.
void __fastcall StatsRecompute(UnitStats*, void*, void*, CUnit*);
// 005D7E90. The AI's memory of attackers (CUnitServer + 0x980): remembers `attacker`.
void __fastcall RememberAttacker(void*, void*, CUnit*);
