// game.exe's addresses as the harness and the vector recorders name them.
//
// The harness names some of the game's tables by game.exe's address (`&OriginalGameData<const std::uint8_t>(address)`:
// the windows' and the AI modes' kinds, harness_windows.h and harness_digest.cpp), the ids the goldens hold. The
// vector recorders (tests/vectors/record_*.cpp; not built since H40, the tag original-oracle has the build that ran
// them in game.exe) also call game.exe's functions by address (EI_ORIGINAL, OriginalCode).
#pragma once

#include <cstdint>

// `EI_NAKED R f(...) EI_ORIGINAL(addr)`: a body that jumps to `addr` with the stack untouched, so any calling
// convention works (32-bit only).
#if defined(_M_IX86)
#define EI_NAKED __declspec(naked)
#define EI_ORIGINAL(addr) \
    { __asm { push addr } __asm { ret } }
#endif

namespace ei {

// game.exe's object at its address. In the harness only its address is taken (an id); the vector recorders read
// game.exe's own copy in its runs. named_data.py lets a literal through only as this call's argument, and only in
// tests/.
template <typename T>
T& OriginalGameData(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

} // namespace ei

namespace ei::verify {

// game.exe's function at `address` as F (a __thiscall member as `R(__thiscall*)(T*, ...)`): the vector recorders'.
template <class F>
F OriginalCode(std::uintptr_t address) {
    return reinterpret_cast<F>(address);
}

} // namespace ei::verify
