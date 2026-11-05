#pragma once

// The float maths functions of the game code, with the same results on every system (docs/notes/float-maths.md,
// "Maths functions"). The systems' own float functions (sinf, cosf, ...) are not correctly rounded and round
// differently: Windows' UCRT's sinf is an ulp off the true value for 0.07% of the floats, glibc's for 0.7%, at other
// inputs, so a game on Linux left Windows' goldens at the main menu's first turning animal. Each function here is the
// system's double function on the float, rounded once to float: for every float the one-argument ones give the
// same results on UCRT and glibc (checked over all 2^32 inputs), the two-argument ones on every sample checked. Hypot
// needs no library: the squares of two floats are exact in double, so the sum and the square root are IEEE's.

#include <cmath>

namespace lib {

inline float Sin(float x) { return static_cast<float>(std::sin(static_cast<double>(x))); }
inline float Cos(float x) { return static_cast<float>(std::cos(static_cast<double>(x))); }
inline float Tan(float x) { return static_cast<float>(std::tan(static_cast<double>(x))); }
inline float Asin(float x) { return static_cast<float>(std::asin(static_cast<double>(x))); }
inline float Acos(float x) { return static_cast<float>(std::acos(static_cast<double>(x))); }
inline float Atan2(float y, float x) {
    return static_cast<float>(std::atan2(static_cast<double>(y), static_cast<double>(x)));
}
inline float Exp(float x) { return static_cast<float>(std::exp(static_cast<double>(x))); }
inline float Log(float x) { return static_cast<float>(std::log(static_cast<double>(x))); }
inline float Log10(float x) { return static_cast<float>(std::log10(static_cast<double>(x))); }
inline float Pow(float base, float exponent) {
    return static_cast<float>(std::pow(static_cast<double>(base), static_cast<double>(exponent)));
}
inline float Hypot(float x, float y) {
    const double dx = x, dy = y;
    return static_cast<float>(std::sqrt(dx * dx + dy * dy));
}

} // namespace lib
