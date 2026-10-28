#pragma once

// STLport pair<T1, T2> constructors and destructors as the exe instantiates them, registered at
// their addresses by stlp_pair.cpp. Thiscalls are __fastcall with an unused EDX; each constructor
// returns this. Several bodies exist more than once (one per object file); one body serves each.

#include "stlp.h"

#include <cstdint>

namespace stlp {

// pair<K, V> of two dwords (K and V an int, an id or a pointer).
struct DwordPair {

    std::uint32_t first;
    std::uint32_t second;
};
static_assert(sizeof(DwordPair) == 8);

// pair<const string, V> with a dword V (a pointer or an int).
struct StringDwordPair {

    string first;
    std::uint32_t second;
};
static_assert(sizeof(StringDwordPair) == 0x10);

// The list element policy (stlp_list.h) of pair<const string, dword> (the link loader's archive list:
// _M_create_node, clear): copied by the pair's constructor, destroyed by freeing the string's block.
struct StringDwordElement {
    static void Destroy(StringDwordPair* p) { p->first.~string(); }
};

// pair<const string, string>.
struct StringPair {
    // ~pair (plain RET): the second string's block freed, then the first's.
    void Destruct();
    // The scalar deleting destructor: ~pair, then operator delete when bit 0 is set.
    StringPair* DeletingDestructor(unsigned flags);

    string first;
    string second;
};
static_assert(sizeof(StringPair) == 0x18);

// pair<const string, list<T*>> (the trader stock's recipe map, item_stock.cpp; first and second
// are named key and mapped, as the hash_map values are).
struct StringListPair {
    // pair(const string&, const list&): the string copied, then list(const list&)
    // inlined: a sentinel, then insert(end(), first, last), one node per element in order.
    StringListPair* Construct(const string& first, const list<void*>& second);
    // ~pair (plain RET): the list's nodes and sentinel back to the pool (the nodes in
    // order), then the string's block.
    void Destruct();

    string key;
    list<void*> mapped;
};
static_assert(sizeof(StringListPair) == 0x10);

} // namespace stlp
