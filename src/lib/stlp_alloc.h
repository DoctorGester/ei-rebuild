#pragma once

// What every STLport family header needs (stlp_list.h, stlp_vector.h): the node allocator and an
// element seen only as its bytes.

#include <cstddef>

namespace stlp {

// The game's __node_alloc: 16 free lists for sizes up to 128 bytes, operator new above that.
void* allocate(std::size_t bytes);
void deallocate(void* p, std::size_t bytes);
// allocate for `count` T from the pool, the raw block typed as T[count] (nothing constructed).
template <class T> T* allocate_as(std::size_t count = 1) { return static_cast<T*>(allocate(count * sizeof(T))); }

// An element as raw bytes, for instantiations seen only through their element size (the size picks
// the pool's free list).
template <std::size_t Bytes>
struct Opaque {
    unsigned char bytes[Bytes];
};

} // namespace stlp
