#pragma once

// The hash tables of 00400000-00440000 (hash_tables_400.cpp): the STLport hashtable layout of the
// one-dword-key tables and the rewritten functions over it.

#include "chunk_archive.h"
#include "net_classes.h"

#include <cstdint>
#include <type_traits>

namespace hash400 {

// An id table: a hash_map of a one-dword key and value (0x14 bytes) and the highest key inserted.
struct Table {
    using Map = stlp::hash_map<std::uint32_t, std::uint32_t, stlp::IntHash>;
    void ObjectsSetAt(const std::int32_t* key, const std::uint32_t* value);
    std::uint32_t ObjectsRemoveKey(const std::uint32_t* key);
    int ObjectsLookup(const std::uint32_t* key, std::uint32_t* value);
    std::uint32_t PositionsRemoveKey(const std::uint32_t* key);
    std::uint32_t* ObjectsFind(const std::uint32_t* key);
    std::uint32_t* PositionAt(const std::int32_t* key);
    std::uint32_t* SpareCountAt(const std::int32_t* key);
    Map map;                    // +0x00
    std::int32_t maxKey;        // +0x14
};
static_assert(sizeof(Table::Map) == 0x14 && offsetof(Table, maxKey) == 0x14);

// An owner's hash_map of a one-dword key and value, followed by its highest key, as an id table.
template <typename Map>
Table* AsTable(Map* map) {
    static_assert(sizeof(Map) == 0x14);
    return reinterpret_cast<Table*>(map);
}

// The id tables' members for an owner's hash_map of a one-dword key and a pointer: operator[]
// (00406A10), the position table's RemoveKey (00405350; the value, null when there is none), the
// objects table's RemoveKey (00404F20 / 00599A80) and find (00406950 / 00599AE0; the value's
// address or null). operator[] keeps the highest key in the word after the map, as the original.
template <typename K, typename V, typename H>
V& PositionAt(stlp::hash_map<K, V, H>& map, std::int32_t key) {
    static_assert(sizeof(K) == 4 && std::is_pointer_v<V>);
    return *reinterpret_cast<V*>(AsTable(&map)->PositionAt(&key));
}
template <typename K, typename V, typename H>
V PositionsRemoveKey(stlp::hash_map<K, V, H>& map, std::uint32_t key) {
    static_assert(sizeof(K) == 4 && std::is_pointer_v<V>);
    return reinterpret_cast<V>(AsTable(&map)->PositionsRemoveKey(&key));
}
template <typename K, typename V, typename H>
V ObjectsRemoveKey(stlp::hash_map<K, V, H>& map, std::uint32_t key) {
    static_assert(sizeof(K) == 4 && std::is_pointer_v<V>);
    return reinterpret_cast<V>(AsTable(&map)->ObjectsRemoveKey(&key));
}
template <typename K, typename V, typename H>
V* ObjectsFind(stlp::hash_map<K, V, H>& map, std::uint32_t key) {
    static_assert(sizeof(K) == 4 && std::is_pointer_v<V>);
    return reinterpret_cast<V*>(AsTable(&map)->ObjectsFind(&key));
}

void* __fastcall ClassRegistryConstruct(ChunkArchive::ClassRegistry* registry);
void __fastcall ClassRegistryDestruct(ChunkArchive::ClassRegistry* registry);
void __fastcall TypeRegistryDestruct(net::TypeRegistry* registry);

} // namespace hash400
