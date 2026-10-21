#pragma once

// The seam for game.exe's data at fixed addresses (its settings, databases, generator, vtables and import
// slots): code names such data by the original's address and reaches it only through GameAddress,
// GameData<T> or GamePointer<T> (S18, docs/notes/standalone.md "Own data"; tools/data_seam.py checks it).
// In game.exe (the original's runs, where the harness and the vector recorders read the original's
// data) that is the address itself. The standalone image keeps game.exe's .bss in ei_hook.dll's
// storage (S18b, src/standalone/image_data.cpp): its addresses move by g_bssShift, a multiple of
// 0x10000 (the low 16 bits of each address stay), set before any static constructor; the other
// sections are still at their addresses. ei_vectors.exe (EI_VECTORS_REPLAY) links the formulas without game.exe;
// there GameData leads to a stand-in its checks fill with a recorded call's values
// (tests/vectors/replay_game_data.cpp; docs/notes/standalone-verification.md, SV4d).

#include <cstddef>
#include <cstdint>

namespace ei {

// The original's address of a data object, kept as a name in a table or constant: resolve it with
// GameAddress (or GameData / GamePointer) before use.
constexpr std::uintptr_t DataId(std::uintptr_t address) { return address; }

// game.exe's .bss, the uninitialised tail of its .data section, as the image moves it: the file's zero
// fill starts at 0079C000, but the key bindings' action table (0079BF70, 0x200 bytes) runs across that
// address, so the move starts at the table (no object runs across 0079BF70). Up to the page end.
constexpr std::uintptr_t kBssBegin = DataId(0x0079BF70);
constexpr std::uintptr_t kBssEnd = DataId(0x007CD000);

#ifdef EI_VECTORS_REPLAY
// Where the data game.exe has at `address` is in this process (ei_vectors: the stand-in's address).
inline std::uintptr_t GameAddress(std::uintptr_t address) { return address; }
#else
// How far game.exe's .bss is from its address in this process: 0 in game.exe, the distance to our
// storage in the image (image_data.cpp).
extern std::uintptr_t g_bssShift;

// Where the data game.exe has at `address` is in this process.
inline std::uintptr_t GameAddress(std::uintptr_t address) {
    return address - kBssBegin < kBssEnd - kBssBegin ? address + g_bssShift : address;
}
#endif

template <typename T>
T* GamePointer(std::uintptr_t address) {
    return reinterpret_cast<T*>(GameAddress(address));
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

// A pointer `offset` bytes into an object whose members are not declared yet (a half-built screen).
inline std::uint8_t* OffsetPtr(void* base, int offset) { return static_cast<std::uint8_t*>(base) + offset; }

} // namespace ei
