#pragma once

// Small application helpers of 00455950-00457720 (app_helpers.cpp, A-E1) and the globals they use.

#include "lib/stlp.h"

#include <cstdint>

namespace app {

// 00784751: whether QuitGame may post the quit message (1 in the exe's data; cleared while a timed
// movie plays). 00784752: the movie mode byte.
inline std::uint8_t& QuitAllowed() { return *reinterpret_cast<std::uint8_t*>(0x00784751); }
inline std::uint8_t& MovieModeFlag() { return *reinterpret_cast<std::uint8_t*>(0x00784752); }

// 0079BC2C: the copy protection's check list (an STLport list of bytes, 1 passed / 0 failed; built
// empty by the static initialiser 00455E50, freed at exit by 00455F00). Nothing adds to it.
using ProtectionCheckList = stlp::list<std::uint8_t>;
inline ProtectionCheckList& ProtectionChecks() { return *reinterpret_cast<ProtectionCheckList*>(0x0079BC2C); }

double __fastcall DiskFreeSpace(const char* path);          // 00455A90
void __fastcall SetQuitAllowed(std::uint8_t allowed);       // 00455F10
bool __fastcall QuitGame(int exitCode);                     // 00455F20
std::uint8_t __cdecl MovieMode();                           // 00455E20
void __cdecl ScrambleObjects();                             // 00455BA0
void __cdecl DelayedProtectionCheck();                      // 00455B50
void __stdcall ConstructProtectionChecks();                 // 00455E50

} // namespace app
