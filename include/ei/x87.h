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

inline bool IsNaN(double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (bits & 0x000FFFFFFFFFFFFFull) != 0;
}

// The condition bits FCOM / FCOMP leave for FNSTSW AX, as the original reads them from AH: C0 "less or
// unordered", C3 "equal or unordered"; unordered (a NaN) sets C0, C2 and C3.
constexpr int kC0 = 0x01;
constexpr int kC2 = 0x04;
constexpr int kC3 = 0x40;
inline int CompareStatus(double a, double b) {
    if (IsNaN(a) || IsNaN(b)) return kC0 | kC2 | kC3;
    if (a < b) return kC0;
    if (a == b) return kC3;
    return 0;
}

// A float loaded and stored back by FLD / FSTP: the same bits, except that a signalling NaN comes back
// quiet. The original copies floats this way.
inline std::uint32_t LoadStoreBits(std::uint32_t bits) {
    const bool nan = (bits & 0x7F800000u) == 0x7F800000u && (bits & 0x007FFFFFu) != 0;
    return nan ? bits | 0x00400000u : bits;
}
inline std::uint32_t LoadStoreBits(const void* from) {
    std::uint32_t bits;
    std::memcpy(&bits, from, sizeof(bits));
    return LoadStoreBits(bits);
}
inline void CopyFloat(void* to, const void* from) {
    const std::uint32_t bits = LoadStoreBits(from);
    std::memcpy(to, &bits, sizeof(bits));
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

// ln x and log10 x as VC6 inlines log / log10: FLDLN2 (FLDLG2); FLD x; FYL2X, unrounded in ST0.
__declspec(naked) inline double __cdecl flog(double) {
    __asm fldln2
    __asm fld qword ptr [esp + 4]
    __asm fyl2x
    __asm ret
}

__declspec(naked) inline double __cdecl flog10(double) {
    __asm fldlg2
    __asm fld qword ptr [esp + 4]
    __asm fyl2x
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

// The control word as the FPU holds it (FNSTCW / FLDCW): bits 0-5 the exception masks, 8-9 the
// precision, 10-11 the rounding, 12 the infinity control; bit 6 reads as 1 and bits 7 and 13-15 as 0
// whatever was loaded. The CRT's __control87_2 runs the same two instructions around converting the
// word to its abstract form and back, which keeps every bit the FPU keeps, and costs ~10x more
// (SV10i: the libcrt maths around every Vector3::Length).
constexpr std::uint32_t kExceptionMasks = 0x003F;
constexpr std::uint32_t kPrecisionBits = 0x0300;
constexpr std::uint32_t kPrecision24 = 0x0000;
constexpr std::uint32_t kPrecision53 = 0x0200;
constexpr std::uint32_t kPrecision64 = 0x0300;
constexpr std::uint32_t kRoundingBits = 0x0C00;
constexpr std::uint32_t kRoundNear = 0x0000;
constexpr std::uint32_t kRoundDown = 0x0400;
constexpr std::uint32_t kRoundUp = 0x0800;
constexpr std::uint32_t kRoundChop = 0x0C00;

__declspec(naked) inline std::uint32_t __cdecl ControlWord() {
    __asm push 0
    __asm fnstcw word ptr [esp]
    __asm pop eax
    __asm ret
}

__declspec(naked) inline void __cdecl SetControlWord(std::uint32_t /*word*/) {
    __asm fldcw word ptr [esp + 4]
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
