#pragma once

// The STLport containers compiled into game.exe that our code still uses (vector, list, deque, tree, the
// hashtable of stlp_hash.h): STLport's layout, the game's pooled node allocator, STLport's hash and order. Each
// member names the STLport routine it mirrors.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>

#include "stlp_alloc.h"
#include "stlp_deque.h"
#include "stlp_list.h"
#include "stlp_tree.h"
#include "stlp_vector.h"
#include "game/game_data.h"

namespace stlp {

// allocate / deallocate: stlp_alloc.h. deallocate(node, 12) inlined: the node back on the node allocator's
// free list of 12-byte nodes (list nodes).
template <class Node>
void PushSmallNode(Node* node) {
    deallocate(node, 12);
}

// Next STLport bucket count >= n (__stl_prime_list).
std::size_t NextPrime(std::size_t n);

} // namespace stlp
