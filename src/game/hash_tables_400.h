#pragma once

// The hash tables of one-dword keys: the id tables' members and the registries' constructor and destructors.

#include "chunk_archive.h"
#include "net_classes.h"


namespace hash400 {

// The id tables: an owner's hash_map of a one-dword key, the highest key inserted kept by the owner in the member
// after it (CBaseManager's tables, the worlds' object maps, the client world's dynamic nodes), with the map-style
// members the game's instantiations give them.

// operator[] keeping the highest key: `key`'s value, a new {key, 0} entry when there is none.
template <typename Map>
typename Map::mapped_type& Entry(Map& map, int& maxKey, int key) {
    if (key > maxKey) maxKey = key;
    return map[static_cast<typename Map::key_type>(key)];
}
template <typename Map>
void SetAt(Map& map, int& maxKey, int key, typename Map::mapped_type value) {
    Entry(map, maxKey, key) = value;
}
// RemoveKey: `key`'s value (zero when there is none), its entry erased.
template <typename Map>
typename Map::mapped_type RemoveKey(Map& map, typename Map::key_type key) {
    auto node = map.find(key);
    if (node == map.end()) return {};
    typename Map::mapped_type value = node->second;
    map.erase(node);
    return value;
}
// Lookup: 1 with `key`'s value in `value`, or 0.
template <typename Map>
int Lookup(const Map& map, typename Map::key_type key, typename Map::mapped_type& value) {
    auto node = map.find(key);
    if (node == map.end()) return 0;
    value = node->second;
    return 1;
}
// The address of `key`'s value, or null.
template <typename Map>
typename Map::mapped_type* Find(Map& map, typename Map::key_type key) {
    auto node = map.find(key);
    return node == map.end() ? nullptr : &node->second;
}


} // namespace hash400
