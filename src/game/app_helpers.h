#pragma once

// Small application helpers of 00455950-00457720 (app_helpers.cpp, A-E1) and the globals they use.

#include "app.h"
#include "lib/stlp.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstdint>

extern lib::StaticObject<stlp::list<std::uint8_t>> g_protectionChecks;   // static_lists.cpp

namespace app {

extern std::uint8_t g_movieMode;   // app_helpers.cpp

// The copy protection's check list (an STLport list of bytes, 1 passed / 0 failed; built
// empty by the static initialiser 00455E30, freed at exit by 00455F00; static_lists.cpp). Nothing adds to it.
using ProtectionCheckList = stlp::list<std::uint8_t>;
inline ProtectionCheckList& ProtectionChecks() { return *g_protectionChecks; }

double __fastcall DiskFreeSpace(const char* path);
void __fastcall SetQuitAllowed(std::uint8_t allowed);
bool __fastcall QuitGame(int exitCode);
std::uint8_t __cdecl MovieMode();
void __cdecl ScrambleObjects();
void __cdecl DelayedProtectionCheck();
void __stdcall ConstructProtectionChecks();

// 00458570 (RET 8; app_registry.cpp): a class the chunk archives can hold, by id and factory.
void __fastcall TypeRegistryRegister(void* self, void*, std::uint32_t id, void* (__cdecl* factory)());
} // namespace app
