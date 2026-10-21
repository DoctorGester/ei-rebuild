#pragma once

// The seam for game.exe's data at fixed addresses (its settings, databases, generator, vtables and import
// slots): code names such data by the original's address and reaches it only through GameAddress,
// GameData<T> or GamePointer<T> (S18, docs/notes/standalone.md "Own data"; tools/data_seam.py checks it).
// In game.exe (the original's runs, where the harness and the vector recorders read the original's
// data) that is the address itself. The standalone image keeps game.exe's .rdata (its import address table
// included) and .data (its .bss included) in ei_hook.dll's storage (S18b, S18d, S18f, S18h;
// src/standalone/image_data.cpp, image_imports.cpp): their addresses move by g_dataShift, a multiple of 0x10000
// (the low 16 bits of each address stay), set before any static constructor.
// ei_vectors.exe (EI_VECTORS_REPLAY) links the formulas without game.exe;
// there GameData leads to a stand-in its checks fill with a recorded call's values
// (tests/vectors/replay_game_data.cpp; docs/notes/standalone-verification.md, SV4d).

#include <cstddef>
#include <cstdint>

namespace ei {

// The original's address of a data object, kept as a name in a table or constant: resolve it with
// GameAddress (or GameData / GamePointer) before use.
constexpr std::uintptr_t DataId(std::uintptr_t address) { return address; }

// What the image moves, whole: game.exe's .rdata (its import address table first) and .data with its zero-filled
// tail (the .bss), 0073B000 up to .rsrc. Both ends are page boundaries (.text ends at 0073A670, .rdata at
// 007828B2, .data's tail at 007CD000).
constexpr std::uintptr_t kMovedBegin = DataId(0x0073B000);
constexpr std::uintptr_t kMovedEnd = DataId(0x007CD000);

// game.exe's whole range in its own process: the headers at its base up to its SizeOfImage (0x3D7000). The image has
// nothing of game.exe there (S18k): the host keeps what of it is free reserved, so a stray use faults and is named.
constexpr std::uintptr_t kImageBegin = DataId(0x00400000);
constexpr std::uintptr_t kImageEnd = DataId(0x007D7000);

// game.exe's import address table, the first 0x820 bytes of .rdata: one slot per import. The image's slots
// are ei_hook.dll's own table (image_imports.cpp, S18f), filled as the DLL initialises.
constexpr std::uintptr_t kImportsBegin = DataId(0x0073B000);
constexpr std::uintptr_t kImportsEnd = DataId(0x0073B820);

#ifdef EI_VECTORS_REPLAY
// Where the data game.exe has at `address` is in this process (ei_vectors: the stand-in's address).
inline std::uintptr_t GameAddress(std::uintptr_t address) { return address; }
inline std::uintptr_t OriginalAddress(std::uintptr_t address) { return address; }
#else
// How far game.exe's .rdata and .data are from their addresses in this process: 0 in game.exe, the distance
// to our storage in the image (image_data.cpp).
extern std::uintptr_t g_dataShift;

// An address of game.exe's range (its headers, code, .rsrc) outside the moved data, asked for in the image: there is
// nothing of game.exe there (S18k), so it is reported and the process ends (image_data.cpp).
[[noreturn]] void StrayGameAddress(std::uintptr_t address);

// Where the data game.exe has at `address` is in this process. A game address is itself only in game.exe (shift 0);
// an address outside game.exe's range (a pointer already resolved, null) is itself in both.
inline std::uintptr_t GameAddress(std::uintptr_t address) {
    if (address - kMovedBegin < kMovedEnd - kMovedBegin) return address + g_dataShift;
    if (g_dataShift && address - kImageBegin < kImageEnd - kImageBegin) StrayGameAddress(address);
    return address;
}

// The original's address of `address`, a pointer into this process's copy of game.exe's data (a vtable read from
// an object, for a message or a label that names it as the original does); any other address as it is.
inline std::uintptr_t OriginalAddress(std::uintptr_t address) {
    return address - (kMovedBegin + g_dataShift) < kMovedEnd - kMovedBegin ? address - g_dataShift : address;
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
