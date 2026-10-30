#pragma once

// The seam for game.exe's data at fixed addresses, for the code that reads the original's own (the harness and the
// vector recorders in game.exe's runs): it names such data by its address and reaches it only through GameData<T> or
// GamePointer<T> (tools/data_seam.py checks it). In game.exe that is the address; ei_vectors.exe (EI_VECTORS_REPLAY)
// leads to a stand-in its checks fill. The image has nothing of game.exe's data. See docs/notes/standalone.md,
// "Own data".

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

#ifdef EI_VECTORS_REPLAY
// The stand-in for `size` bytes at `address` (zero until a check writes them).
void* ReplayGameData(std::uintptr_t address, std::size_t size);

template <typename T>
T& GameData(std::uintptr_t address) {
    return *static_cast<T*>(ReplayGameData(address, sizeof(T)));
}
#else
template <typename T>
T& GameData(std::uintptr_t address) {
    return *GamePointer<T>(address);
}
#endif

} // namespace ei
