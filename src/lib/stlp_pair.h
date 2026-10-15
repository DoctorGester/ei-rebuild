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

// pair<const string, string>.
struct StringPair {
    string first;
    string second;
};
static_assert(sizeof(StringPair) == 0x18);

// pair<const string, list<T*>> (the trader stock's recipe map, item_stock.cpp).
struct StringListPair {
    string first;
    list<void*> second;
};
static_assert(sizeof(StringListPair) == 0x10);

struct PairAbi {
    // pair(const K&, const V&) (00407DE0, 0040A9D0; RET 8).
    static DwordPair* __fastcall ConstructDwords(DwordPair* self, void*, const std::uint32_t* first, const std::uint32_t* second);
    // pair(const string&, const V&) (005729D0, 00689950, 006BC140, 0064F880, 006A7710; RET 8): the
    // string copied (a block of size + 1), then the dword.
    static StringDwordPair* __fastcall ConstructStringDword(StringDwordPair* self, void*, const string* first,
                                                            const std::uint32_t* second);
    // pair(const string&, const string&) (00445170; RET 8): the first string copied, then the second.
    static StringPair* __fastcall ConstructStrings(StringPair* self, void*, const string* first, const string* second);
    // pair(const string&, const list&) (0069C1C0; RET 8): the string copied, then list(const list&)
    // inlined: a sentinel, then insert(end(), first, last) (0069D680), one node per element in order.
    static StringListPair* __fastcall ConstructStringList(StringListPair* self, void*, const string* first,
                                                          const list<void*>* second);
    // ~pair() (00445040; plain RET): the second string's block freed, then the first's.
    static void __fastcall DestroyStrings(StringPair* self);
    // The scalar deleting destructor (004457F0; RET 4): ~pair, then operator delete when bit 0 is set.
    static StringPair* __fastcall DeletingDestructor(StringPair* self, void*, unsigned flags);
};

} // namespace stlp
