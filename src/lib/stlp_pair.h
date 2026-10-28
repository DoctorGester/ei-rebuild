#pragma once

// STLport pair<T1, T2> constructors and destructors as the exe instantiates them
// (stlp_pair.cpp). Thiscalls are __fastcall with an unused EDX; each constructor
// returns this. Several bodies exist more than once (one per object file); one body serves each.

#include "stlp.h"

#include <cstdint>
#include <list>
#include <string>

namespace stlp {

// pair<K, V> of two dwords (K and V an int, an id or a pointer).
struct DwordPair {

    std::uint32_t first;
    std::uint32_t second;
};
static_assert(sizeof(DwordPair) == 8);

// pair<const string, list<T*>> (the trader stock's recipe map, item_stock.cpp; first and second
// are named key and mapped, as the hash_map values are).
struct StringListPair {
    // pair(const string&, const list&): the string copied, then the list.
    StringListPair* Construct(const std::string& first, const std::list<void*>& second);
    // ~pair: the list, then the string.
    void Destruct();

    std::string key;
    std::list<void*> mapped;
};

} // namespace stlp
