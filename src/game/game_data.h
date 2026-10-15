#pragma once

// The seam for game.exe's data at fixed addresses (its settings, databases and generator): the
// named accessors of such globals read them through GameData<T>(address). In the game that is the
// address itself. ei_vectors.exe (EI_VECTORS_REPLAY) links the formulas without game.exe; there the
// address leads to a stand-in its checks fill with a recorded call's values
// (tests/vectors/replay_game_data.cpp; docs/notes/standalone-verification.md, SV4d).

#include <cstddef>
#include <cstdint>

namespace ei {

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
    return *reinterpret_cast<T*>(address);
}
#endif

} // namespace ei
