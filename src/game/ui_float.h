// The float helpers the interface files share: D() for the interface files still built for x87 (CMakeLists'
// EI_X87_DRAW_SOURCES), which multiply in double where the original did (its x87 registers); constants read as
// raw bits; truncation.
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
