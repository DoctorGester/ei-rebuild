#pragma once

// Script floats across the Lua boundary (docs/notes/lua-scripting.md, "Bindings"): a script value is a float, a Lua
// number a double. Bit operations for the NaNs: a conversion through the FPU would quiet a signalling NaN, and the game
// copies floats as bits (Assign), so a NaN keeps its sign and payload both ways.

#include <bit>
#include <cstdint>

namespace scriptlua {

// Exact: every float is a double.
inline double ToLuaNumber(float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t payload = bits & 0x007FFFFFu;
    if ((bits & 0x7F800000u) != 0x7F800000u || payload == 0) return static_cast<double>(value);
    const std::uint64_t sign = static_cast<std::uint64_t>(bits >> 31) << 63;
    return std::bit_cast<double>(sign | 0x7FF0000000000000ull | static_cast<std::uint64_t>(payload) << 29);
}

// The nearest float. A NaN keeps its sign and the top 23 bits of its payload (all of a float's: ToLuaNumber's round
// trip is exact); one whose top 23 payload bits are zero becomes the quiet NaN of its sign.
inline float FromLuaNumber(double value) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    if ((bits & 0x7FF0000000000000ull) != 0x7FF0000000000000ull || (bits & 0x000FFFFFFFFFFFFFull) == 0)
        return static_cast<float>(value);
    std::uint32_t payload = static_cast<std::uint32_t>(bits >> 29) & 0x007FFFFFu;
    if (payload == 0) payload = 0x00400000u;
    return std::bit_cast<float>(static_cast<std::uint32_t>(bits >> 63) << 31 | 0x7F800000u | payload);
}

} // namespace scriptlua
