// The float helpers the interface files share. The interface multiplies in double where the
// original did (the x87 registers) and reads some constants as raw bits.
#pragma once

#include <bit>
#include <concepts>
#include <cstdint>

// A float widened to double: products written with D() round once, at the end.
inline double D(float f) { return f; }

// The float with the given raw bits.
inline float FloatFromBits(std::uint32_t bits) { return std::bit_cast<float>(bits); }
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatFromBits(T) = delete;

// The original truncates through _ftol.
inline int Truncate(float value) { return static_cast<int>(value); }
