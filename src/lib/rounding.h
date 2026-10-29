#pragma once

// A float rounded to an int as the x87 code rounds it on the game's main thread (x87::fistp), for the code built for
// SSE2: to nearest, ties to even; a NaN, an infinity or a value outside the int range gives 0x80000000. CVTSS2SI under
// MXCSR's default rounding, which nothing changes. (std::lround rounds halves away from zero; std::lrint reads the FP
// environment on every call.)

#include <xmmintrin.h>

namespace lib {

inline int RoundEven(float value) { return _mm_cvtss_si32(_mm_set_ss(value)); }

} // namespace lib
