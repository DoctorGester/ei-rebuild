#pragma once

// The hash tables of 00400000-00440000 (hash_tables_400.cpp): the STLport hashtable layout of the
// one-dword-key tables and the rewritten functions over it.

#include "chunk_archive.h"
#include "net_classes.h"

#include <cstdint>

namespace hash400 {

// STLport hashtable (0x14 bytes); the id tables add the highest key inserted.
struct Table {
    void ObjectsSetAt(const std::int32_t* key, const std::uint32_t* value);
    std::uint32_t ObjectsRemoveKey(const std::uint32_t* key);
    int ObjectsLookup(const std::uint32_t* key, std::uint32_t* value);
    std::uint32_t PositionsRemoveKey(const std::uint32_t* key);
    std::uint32_t* ObjectsFind(const std::uint32_t* key);
    std::uint32_t* PositionAt(const std::int32_t* key);
    std::uint32_t* SpareCountAt(const std::int32_t* key);
    std::uint8_t functors[4];   // +0x00 hasher / key_equal / select1st (empty)
    void** buckets;             // +0x04
    void** bucketsEnd;          // +0x08
    void** bucketsEos;          // +0x0C
    std::uint32_t count;        // +0x10
    std::int32_t maxKey;        // +0x14 the id tables only
};
static_assert(offsetof(Table, count) == 0x10);

// The registries' two hash_maps are this layout (without the id tables' extra +0x14 word).
template <typename Map>
Table* AsTable(Map* map) {
    static_assert(sizeof(Map) == 0x14);
    return reinterpret_cast<Table*>(map);
}

void* __fastcall ClassRegistryConstruct(ChunkArchive::ClassRegistry* registry);
void __fastcall ClassRegistryDestruct(ChunkArchive::ClassRegistry* registry);
void __fastcall TypeRegistryDestruct(net::TypeRegistry* registry);

} // namespace hash400
