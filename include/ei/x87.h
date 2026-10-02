#pragma once

// x87 transcendental instructions, exactly as the original code used them.
//
// The modern CRT's sin/cos/sqrt use SSE2 code paths and do not round the same way as FSIN/FCOS/
// FSQRT, so game maths must go through these. Arguments arrive as double (exact for the float and
// 0.5-scaled values the game passes) and the result is returned in ST0 without being rounded.

namespace x87 {

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

} // namespace x87
