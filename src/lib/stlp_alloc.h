#pragma once

// What every STLport family header needs (stlp_list.h, stlp_vector.h): the node allocator and an
// element seen only as its bytes.

#include <cstddef>

namespace stlp {

// The game's __node_alloc: 16 free lists for sizes up to 128 bytes, operator new above that.
void* allocate(std::size_t bytes);                  // 00405950
void deallocate(void* p, std::size_t bytes);        // 00404D70

// An element as raw bytes, for instantiations seen only through their element size (the size picks
// the pool's free list).
template <std::size_t Bytes>
struct Opaque {
    unsigned char bytes[Bytes];
};

} // namespace stlp
