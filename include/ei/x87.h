#pragma once

// x87 transcendental instructions, exactly as the original code used them.
//
// The modern CRT's sin/cos/sqrt use SSE2 code paths and do not round the same way as FSIN/FCOS/
// FSQRT, so game maths must go through these. Arguments arrive as double (exact for the float and
// 0.5-scaled values the game passes) and the result is returned in ST0 without being rounded.

#include <cstdint>
#include <cstring>

namespace x87 {

// NaN test on the bits. Under /fp:fast MSVC folds `v != v` to false, so the usual idiom silently
// stops detecting NaN (found in the sort verify of world_client); use this instead.
inline bool IsNaN(float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0;
}

__declspec(naked) inline double __cdecl fsin(double) {
    __asm fld qword ptr [esp + 4]
    __asm fsin
    __asm ret
}

__declspec(naked) inline double __cdecl fcos(double) {
    __asm fld qword ptr [esp + 4]
    __asm fcos
    __asm ret
}

__declspec(naked) inline double __cdecl fsqrt(double) {
    __asm fld qword ptr [esp + 4]
    __asm fsqrt
    __asm ret
}

// FISTP: float to int using the FPU's current rounding mode (round-to-nearest by default), unlike
// a C++ cast, which truncates.
__declspec(naked) inline int __cdecl fistp(float) {
    __asm sub esp, 4
    __asm fld dword ptr [esp + 8]
    __asm fistp dword ptr [esp]
    __asm pop eax
    __asm ret
}

} // namespace x87
