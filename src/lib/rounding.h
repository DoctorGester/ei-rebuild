#pragma once

// A float rounded to an int as the original's x87 code rounds it on the game's main thread: to nearest, ties
// to even; a NaN, an infinity or a value outside the int range gives 0x80000000 (as CVTSS2SI). Plain float
// maths (rule 7): below 2^23, adding 2^23 (with the value's sign) and taking it away again leaves the whole number the
// default rounding picks; from 2^23 on every float is whole. (std::lround rounds halves away from zero; std::lrint and
// std::nearbyint are CRT calls on MSVC that read the FP environment on every call.)

#include <bit>
#include <cstdint>

namespace lib {

inline int RoundEven(float value) {
    constexpr float kLimit = 2147483648.0f;   // 2^31
    constexpr float kWhole = 8388608.0f;      // 2^23
    if (!(value >= -kLimit && value < kLimit)) return INT32_MIN;
    if (!(value > -kWhole && value < kWhole)) return static_cast<int>(value);   // whole already
    // 2^23 with the value's sign, by its bits: MSVC made a compare on the sign a branch (4x slower on mixed signs).
    const float shift =
        std::bit_cast<float>(std::bit_cast<std::uint32_t>(kWhole) | (std::bit_cast<std::uint32_t>(value) & 0x80000000u));
    return static_cast<int>((value + shift) - shift);
}

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
