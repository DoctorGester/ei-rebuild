#pragma once

// Helpers of the list<T> checks (verify_stlp_list.cpp, verify_stlp_list_refs.cpp).

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace stlp_verify {

// The node allocator's free list for blocks of `bytes` (16 lists of 8-byte steps from 0079B224);
// null above 128 bytes, which come from operator new.
inline void** FreeList(std::size_t bytes) {
    if (bytes > 128) return nullptr;
    return reinterpret_cast<void**>(0x0079B224 + 4 * ((bytes - 1) >> 3));
}

// One report name per instantiation ("stlp::list::end 004049D0"), so the counts show which are
// reached. Each address is checked under one kind only.
template <std::uintptr_t A>
const char* Name(const char* kind) {
    static char name[64];
    static const bool once = (std::snprintf(name, sizeof(name), "%s %08X", kind, static_cast<unsigned>(A)), true);
    (void)once;
    return name;
}

// `node` linked in between `prev` and `next`, both ways.
template <typename N>
bool LinkedBetween(const N* node, const N* prev, const N* next) {
    return node->prev == prev && node->next == next && prev->next == node && next->prev == node;
}

} // namespace stlp_verify
