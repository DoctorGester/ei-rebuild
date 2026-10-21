#pragma once

#include <cstdint>
#include <cstring>

#include "game/game_data.h"

// Calls virtual method `slot` of an original game object through its own vtable, for classes whose
// full virtual interface is not declared yet. All arguments must be 32-bit (pointers, ints).
// __thiscall is called as __fastcall with an unused EDX, which has the same stack behaviour.

namespace ei {

template <typename R, typename... Args>
R VCall(const void* object, int slot, Args... args) {
    void* fn = (*reinterpret_cast<void* const* const*>(object))[slot];
    return reinterpret_cast<R(__fastcall*)(const void*, void*, Args...)>(fn)(object, nullptr, args...);
}

// Stores the address of one of the game's vtables into an object, as the constructor and destructor
// bodies we write out by hand do (the classes' C++ vtables are never instantiated in the hook build). The
// address is game.exe's, resolved through the data seam (game_data.h): the image keeps .rdata elsewhere.
inline void SetVtable(void* object, std::uintptr_t vtable) {
    const std::uintptr_t at = GameAddress(vtable);
    std::memcpy(object, &at, sizeof(at));
}
// The same, from a vtable named by its class's accessor (`Class::Vtable()`).
inline void SetVtable(void* object, const void* vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

} // namespace ei
