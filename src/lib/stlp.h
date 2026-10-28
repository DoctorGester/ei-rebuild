#pragma once

// The game's pooled node allocator (STLport's __node_alloc) that our code still uses.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>

#include "stlp_alloc.h"
#include "game/game_data.h"

namespace stlp {

// allocate / deallocate: stlp_alloc.h. deallocate(node, 12) inlined: the node back on the node allocator's
// free list of 12-byte nodes (list nodes).
template <class Node>
void PushSmallNode(Node* node) {
    deallocate(node, 12);
}

} // namespace stlp
