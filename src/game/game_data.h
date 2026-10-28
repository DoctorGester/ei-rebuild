#pragma once

// The seam for game.exe's data at fixed addresses: code names such data by its address and reaches it only through
// GameAddress, GameData<T> or GamePointer<T> (tools/data_seam.py checks it). In the image the moved data sits at
// the address plus g_dataShift; ei_vectors.exe (EI_VECTORS_REPLAY) leads to a stand-in its checks fill. See
// docs/notes/standalone.md, "Own data".

#include <cstddef>
#include <cstdint>

namespace ei {

// A game address of a data object, kept as a name in a table or constant: resolve it with GameAddress (or
// GameData / GamePointer) before use.
constexpr std::uintptr_t DataId(std::uintptr_t address) { return address; }

// What the image moves, whole: game.exe's .rdata (its import address table first) and .data with its zero-filled
// tail (the .bss). Both ends are page boundaries.
constexpr std::uintptr_t kMovedBegin = DataId(0x0073B000);
constexpr std::uintptr_t kMovedEnd = DataId(0x007CD000);

// game.exe's whole range in its own process. The host keeps what of it is free reserved, so a stray use faults.
constexpr std::uintptr_t kImageBegin = DataId(0x00400000);
constexpr std::uintptr_t kImageEnd = DataId(0x007D7000);

// game.exe's import address table, the first 0x820 bytes of .rdata.
constexpr std::uintptr_t kImportsBegin = DataId(0x0073B000);
constexpr std::uintptr_t kImportsEnd = DataId(0x0073B820);

#ifdef EI_VECTORS_REPLAY
// Where the data game.exe has at `address` is in this process (ei_vectors: the stand-in's address).
inline std::uintptr_t GameAddress(std::uintptr_t address) { return address; }
inline std::uintptr_t OriginalAddress(std::uintptr_t address) { return address; }
#else
// How far game.exe's .rdata and .data are from their addresses in this process: 0 in game.exe, the distance
// to our storage in the image.
extern std::uintptr_t g_dataShift;

// An address of game.exe's range outside the moved data, asked for in the image: reported, and the process ends.
[[noreturn]] void StrayGameAddress(std::uintptr_t address);

// Where the data game.exe has at `address` is in this process. An address outside game.exe's range (a pointer
// already resolved, null) is itself.
inline std::uintptr_t GameAddress(std::uintptr_t address) {
    if (address - kMovedBegin < kMovedEnd - kMovedBegin) return address + g_dataShift;
    if (g_dataShift && address - kImageBegin < kImageEnd - kImageBegin) StrayGameAddress(address);
    return address;
}

// The game address of `address`, a pointer into this process's copy of game.exe's data (for a message or a label).
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


// Slot `slot` of a hand-made vtable (an array of code pointers) as the function type Fn.
template <typename Fn>
Fn VirtualSlot(const void* const* vtable, int slot) {
    return reinterpret_cast<Fn>(vtable[slot]);
}
// Slot `slot` of an object whose first dword is its vtable pointer, as the function type Fn.
template <typename Fn>
Fn ObjectSlot(const void* object, int slot) {
    return VirtualSlot<Fn>(*static_cast<const void* const* const*>(object), slot);
}

} // namespace ei
