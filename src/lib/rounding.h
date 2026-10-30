#pragma once

// A float rounded to an int as the original's x87 code rounds it on the game's main thread: to nearest, ties
// to even; a NaN, an infinity or a value outside the int range gives 0x80000000. CVTSS2SI under
// MXCSR's default rounding, which nothing changes. (std::lround rounds halves away from zero; std::lrint reads the FP
// environment on every call.)

#include <xmmintrin.h>

#include <cstdint>

namespace lib {

inline int RoundEven(float value) { return _mm_cvtss_si32(_mm_set_ss(value)); }

// A float or double truncated as the original's _ftol does: through a 64-bit integer, keeping its low half. A NaN or
// a value outside the 64-bit range gives 0 (the conversion's 0x8000000000000000 has a zero low half; a cast to int
// gives 0x80000000).
inline int Truncate(double value) {
    constexpr double kLimit = 9223372036854775808.0;   // 2^63
    if (!(value >= -kLimit && value < kLimit)) return 0;
    return static_cast<int>(static_cast<std::int64_t>(value));
}
inline int Truncate(float value) { return Truncate(static_cast<double>(value)); }

} // namespace lib
