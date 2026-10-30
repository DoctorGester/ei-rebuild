// The original game's functions at their game.exe addresses, for the runs of the original (the harness's
// --no-hooks runs, the vector recorders' EI_VECTORS=1 runs). game.exe's side of the harness calls only game.exe's own
// functions and host code, never ours (H30): run_all's reference cache keys a run of the original by the harness's
// sources alone (tools/run_all.py, reference_key), so a change of the game's code never plays the original again.
// game.exe only: the standalone image has no code at game.exe's addresses (its guarded code section reports such a
// call), so the harness calls ours by name there (ei::Standalone() ? ours : the original).
#pragma once

#include "ei/hook.h"

#include <cstdint>

// `EI_NAKED R f(...) EI_ORIGINAL(addr)`: a body that jumps to `addr` with the stack untouched, so any calling
// convention works. In an x64 build (build.cmd verify-x64) game.exe's side of the harness never runs (game.exe is
// 32-bit) but still compiles: no naked functions there, and a body that reports the call and ends the run.
#if defined(_M_IX86)
#define EI_NAKED __declspec(naked)
#define EI_ORIGINAL(addr) \
    { __asm { push addr } __asm { ret } }
#else
#define EI_NAKED
#define EI_ORIGINAL(addr) \
    { ::ei::verify::NoOriginalCode(addr); }
#endif

namespace ei {

// game.exe's own copy of an object at its address, for the harness in game.exe's runs (--no-hooks, EI_VECTORS=1): there
// the game uses its copy and our named global is never touched. Not in the image (nothing of game.exe is there).
// In the harness T is a view of game.exe's own (exe_objects.h) or a plain type, never a class of ours (the vector
// recorders, tests/vectors, read our classes: they record our functions' inputs).
// named_data.py lets a literal through only as this call's argument, and only in tests/.
template <typename T>
T& OriginalGameData(std::uintptr_t address) {
    return *reinterpret_cast<T*>(address);
}

} // namespace ei

namespace ei::verify {

// The x64 build's body of a call of game.exe's code (EI_ORIGINAL, the inline steps): logs it and ends the run.
[[noreturn]] void NoOriginalCode(std::uintptr_t address);

// game.exe's function at `address` as F (a __thiscall member as `R(__thiscall*)(T*, ...)`). game.exe's runs only.
template <class F>
F OriginalCode(std::uintptr_t address) {
    return reinterpret_cast<F>(address);
}

// game.exe's CRT rand() seed of the calling thread: _tiddata::_holdrand, +0x14 of what game.exe's _getptd
// (006F00D1, which allocates the block on first use) returns. game.exe's runs only.
inline unsigned& OriginalRandState() {
    auto* data = OriginalCode<unsigned char*(__cdecl*)()>(0x006F00D1)();
    return *reinterpret_cast<unsigned*>(data + 0x14);
}

} // namespace ei::verify
