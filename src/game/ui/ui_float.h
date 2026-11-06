// The float helpers the interface files share: constants read as raw bits; truncation.
#pragma once

#include <bit>
#include <concepts>
#include <cstdint>

// The float with the given raw bits.
inline float FloatFromBits(std::uint32_t bits) { return std::bit_cast<float>(bits); }
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatFromBits(T) = delete;

// The original truncates through _ftol.
inline int Truncate(float value) { return static_cast<int>(value); }
