#pragma once

// The STLport containers compiled into game.exe that our code still uses (list): STLport's layout and the
// game's pooled node allocator. Each member names the STLport routine it mirrors.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>

#include "stlp_alloc.h"
#include "stlp_list.h"
#include "game/game_data.h"

namespace stlp {

// allocate / deallocate: stlp_alloc.h. deallocate(node, 12) inlined: the node back on the node allocator's
// free list of 12-byte nodes (list nodes).
template <class Node>
void PushSmallNode(Node* node) {
    deallocate(node, 12);
}

} // namespace stlp
