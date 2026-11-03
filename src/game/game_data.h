#pragma once

// The seam for game.exe's data at fixed addresses, for the code that named the original's own: such data is named by
// its address and reached only through GameData<T> or GamePointer<T> (tools/data_seam.py checks it). The image has
// nothing of game.exe's data. See docs/notes/standalone.md, "Own data".

#include <cstddef>
#include <cstdint>

namespace ei {

// A game address of a data object, kept as a name in a table or constant: resolve it with GameData or GamePointer
// before use.
constexpr std::uintptr_t DataId(std::uintptr_t address) { return address; }

template <typename T>
T* GamePointer(std::uintptr_t address) {
    return reinterpret_cast<T*>(address);
}

template <typename T>
T& GameData(std::uintptr_t address) {
    return *GamePointer<T>(address);
}

} // namespace ei
