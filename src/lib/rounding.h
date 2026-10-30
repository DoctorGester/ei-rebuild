#pragma once

// A float rounded to an int as the original's x87 code rounds it on the game's main thread: to nearest, ties
// to even; a NaN, an infinity or a value outside the int range gives 0x80000000. CVTSS2SI under
// MXCSR's default rounding, which nothing changes. (std::lround rounds halves away from zero; std::lrint reads the FP
// environment on every call.)

#include <xmmintrin.h>

#include <cstdint>

namespace lib {

inline int RoundEven(float value) { return _mm_cvtss_si32(_mm_set_ss(value)); }

// A float or double truncated as the original's _ftol does: through a 64-bit integer, keeping its low half (a NaN,
// or a value beyond the 64-bit range, gives 0, where a cast to int gives 0x80000000).
inline int Truncate(float value) { return static_cast<int>(static_cast<std::int64_t>(value)); }
inline int Truncate(double value) { return static_cast<int>(static_cast<std::int64_t>(value)); }

} // namespace lib
