#pragma once

// Small application helpers (app_helpers.cpp) and the globals they use.

#include "app.h"
#include "chunk_archive.h"

#include <cstdint>
#include <list>

namespace app {

extern std::uint8_t g_movieMode;   // app_helpers.cpp


double DiskFreeSpace(const char* path);
bool QuitGame(int exitCode);
void DelayedProtectionCheck();

// (app_registry.cpp): a class the chunk archives can hold, by id and factory.
void TypeRegistryRegister(ChunkArchive::ClassRegistry* registry, std::uint32_t id,
                          ChunkArchive::Factory factory);
} // namespace app
