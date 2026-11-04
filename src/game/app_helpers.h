#pragma once

// The chunk archives' class registration (app_frame.cpp).

#include "app.h"
#include "chunk_archive.h"

#include <cstdint>
#include <list>

namespace app {


// A class the chunk archives can hold, by id and factory.
void TypeRegistryRegister(ChunkArchive::ClassRegistry* registry, std::uint32_t id,
                          ChunkArchive::Factory factory);
} // namespace app
