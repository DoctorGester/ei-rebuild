#pragma once

// Helpers for the std::vector the game's containers are (the former engine arrays in 32-byte aligned blocks are
// vectors). Which original instantiation each address belongs to:
// docs/notes/core.md, "Shared containers".

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <utility>
#include <vector>

namespace lib {

// Frees a vector's block and leaves it empty (the exit functions of the static work buffers, the owners' release
// paths; `v = {}` and clear() keep the storage).
template <typename T>
void FreeStorage(std::vector<T>& v) {
    std::vector<T>().swap(v);
}

// CList::Find as a test: whether `c` holds `value`.
template <typename C, typename T>
bool Contains(const C& c, const T& value) {
    return std::find(std::begin(c), std::end(c), value) != std::end(c);
}

// CArray's SetAtGrow on a vector: grows it to hold `index` (the new elements value-initialised), then sets it.
template <typename T, typename U>
void SetAtGrow(std::vector<T>& v, int index, U&& value) {
    if (index >= std::ssize(v)) v.resize(static_cast<std::size_t>(index) + 1);
    v[static_cast<std::size_t>(index)] = std::forward<U>(value);
}

} // namespace lib
