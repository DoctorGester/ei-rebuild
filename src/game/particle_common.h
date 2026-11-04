#pragma once

// What the particle files (particle_*.cpp) share: the random scales that are not a Range draw.
//
// A draw g_clientRandom.Range(lo, hi) is (hi - lo) * word * 2^-32 + lo. The original wrote most of its draws as
// word * k + lo with k = (hi - lo) * 2^-32 stored; where k is exactly a float's (hi - lo) times 2^-32 that is the
// same value, and the code calls Range. The scales below carry a stray low bit (k is not any float times 2^-32
// that reads as a round number), so their draws stay word * k + lo; the name says the range they scale a word
// to, the static_assert holds the original's bits.

#include <bit>
#include <cstdint>

// [0, 0.4): 0.4f * 2^-32 is 0x2ECCCCCD.
constexpr float kWordTo0_4 = 9.3132266e-11f;
static_assert(std::bit_cast<std::uint32_t>(kWordTo0_4) == 0x2ECCCCCE);
// [0, 0.6): 0.6f * 2^-32 is 0x2F19999A.
constexpr float kWordTo0_6 = 1.3969838e-10f;
static_assert(std::bit_cast<std::uint32_t>(kWordTo0_6) == 0x2F199999);
// [0, 0.8): 0.8f * 2^-32 is 0x2F4CCCCD.
constexpr float kWordTo0_8 = 1.862645e-10f;
static_assert(std::bit_cast<std::uint32_t>(kWordTo0_8) == 0x2F4CCCCC);
// [0, 0.2): 0.2f * 2^-32 is 0x2E4CCCCD.
constexpr float kWordTo0_2 = 4.656614e-11f;
static_assert(std::bit_cast<std::uint32_t>(kWordTo0_2) == 0x2E4CCCD0);
// [0, 0.2) once more, a different stray bit (Falling's gusts).
constexpr float kWordTo0_2Gust = 4.6566133e-11f;
static_assert(std::bit_cast<std::uint32_t>(kWordTo0_2Gust) == 0x2E4CCCCE);
// [0, 0.3): 0.3f * 2^-32 is 0x2E99999A.
constexpr float kWordTo0_3 = 6.984918e-11f;
static_assert(std::bit_cast<std::uint32_t>(kWordTo0_3) == 0x2E999998);
// [0, 0.35): 0.35f * 2^-32 is 0x2EB33333.
constexpr float kWordTo0_35 = 8.149073e-11f;
static_assert(std::bit_cast<std::uint32_t>(kWordTo0_35) == 0x2EB33334);
