#pragma once

// Small application helpers (app_helpers.cpp) and the globals they use.

#include "app.h"
#include "chunk_archive.h"
#include "lib/stlp.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstdint>
#include <list>

extern lib::StaticObject<std::list<std::uint8_t>> g_protectionChecks;   // static_lists.cpp

namespace app {

extern std::uint8_t g_movieMode;   // app_helpers.cpp

// The copy protection's check list (an STLport list of bytes, 1 passed / 0 failed; built empty by
// a static initialiser, freed at exit; static_lists.cpp). Nothing adds to it.
using ProtectionCheckList = std::list<std::uint8_t>;
inline ProtectionCheckList& ProtectionChecks() { return *g_protectionChecks; }

double DiskFreeSpace(const char* path);
bool QuitGame(int exitCode);
std::uint8_t MovieMode();
void ScrambleObjects();
void DelayedProtectionCheck();
void ConstructProtectionChecks();

// (app_registry.cpp): a class the chunk archives can hold, by id and factory.
void TypeRegistryRegister(ChunkArchive::ClassRegistry* registry, void*, std::uint32_t id,
                                     ChunkArchive::Factory factory);
} // namespace app
