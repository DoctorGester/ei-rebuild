#pragma once

// The hash tables of one-dword keys: the id tables' members and the registries' constructor and destructors.

#include "chunk_archive.h"
#include "net_classes.h"


namespace hash400 {

// The id tables: an owner's hash_map of a one-dword key, the highest key inserted kept by the owner in the member
// after it (CBaseManager's tables, the worlds' object maps, the client world's dynamic nodes), with the MFC-style
// members the game's instantiations give them.

// operator[] keeping the highest key: `key`'s value, a new {key, 0} entry when there is none.
template <typename Map>
typename Map::mapped_type& Entry(Map& map, int& maxKey, int key) {
    if (key > maxKey) maxKey = key;
    return map.FindOrInsert({static_cast<typename Map::key_type>(key), {}})->value;
}
template <typename Map>
void SetAt(Map& map, int& maxKey, int key, typename Map::mapped_type value) {
    Entry(map, maxKey, key) = value;
}
// RemoveKey: `key`'s value (zero when there is none), its entry erased.
template <typename Map>
typename Map::mapped_type RemoveKey(Map& map, typename Map::key_type key) {
    auto* node = map.Find(key);
    if (!node) return {};
    typename Map::mapped_type value = node->value;
    map.Erase(node);
    return value;
}
// Lookup: 1 with `key`'s value in `value`, or 0.
template <typename Map>
int Lookup(const Map& map, typename Map::key_type key, typename Map::mapped_type& value) {
    auto* node = map.Find(key);
    if (!node) return 0;
    value = node->value;
    return 1;
}
// The address of `key`'s value, or null.
template <typename Map>
typename Map::mapped_type* Find(Map& map, typename Map::key_type key) {
    auto* node = map.Find(key);
    return node ? &node->value : nullptr;
}

void* ClassRegistryConstruct(ChunkArchive::ClassRegistry* registry);
void ClassRegistryDestruct(ChunkArchive::ClassRegistry* registry);

} // namespace hash400
