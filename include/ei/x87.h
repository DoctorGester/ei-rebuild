#pragma once

// x87 transcendental instructions, exactly as the original code used them.
//
// The modern CRT's sin/cos/sqrt use SSE2 code paths and do not round the same way as FSIN/FCOS/
// FSQRT, so game maths must go through these. Arguments arrive as double (exact for the float and
// 0.5-scaled values the game passes) and the result is returned in ST0 without being rounded.

#include <cstdint>
#include <cstring>

namespace x87 {

// An unsigned 32-bit value as a double, exactly as the original loads it (FILD of the zero-extended
// qword). `static_cast<double>(u)` and even `static_cast<double>(static_cast<__int64>(u))` compile
// to FILD dword + FADD 2^32 for values >= 2^31, and that FADD rounds at the FPU's precision (24 bits
// in vanilla). tools/check_x87.py fails the build if that FADD appears anywhere.
inline double FromUnsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

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

// e^x the way VC6 inlines exp: 2^(x log2 e) through FRNDINT / F2XM1 / FSCALE, unrounded in ST0.
__declspec(naked) inline double __cdecl fexp(double) {
    __asm fld qword ptr [esp + 4]
    __asm fldl2e
    __asm fmulp st(1), st
    __asm fld st(0)
    __asm frndint
    __asm fxch st(1)
    __asm fsub st, st(1)
    __asm f2xm1
    __asm fld1
    __asm faddp st(1), st
    __asm fscale
    __asm fstp st(1)
    __asm ret
}

// FPATAN: atan2(y, x), unrounded (FPATAN ignores the precision control). std::atan2 only becomes
// FPATAN under /fp:fast; under precise semantics it calls the modern CRT.
__declspec(naked) inline double __cdecl fpatan(double /*y*/, double /*x*/) {
    __asm fld qword ptr [esp + 4]
    __asm fld qword ptr [esp + 12]
    __asm fpatan
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
