#pragma once

// Float sign tests and the original's branch-free selects, once. The game compiles a float min / max /
// clamp to a compare and a mask, so which operand wins when the compare is unordered (a
// NaN) or a tie is part of the behaviour. The helpers write the compare the original makes; std::min /
// std::max pick the other operand on a tie (std::min(+0.0f, -0.0f) is +0.0f, MinKeepA(+0.0f, -0.0f) is
// -0.0f), so they are not a drop-in.
//
//   MinKeepA(a, b)  a >= b ? b : a    the lower; a when unordered
//   MaxKeepB(a, b)  a >= b ? a : b    the higher; b when unordered
//   MinKeepB(a, b)  a <  b ? a : b    the lower; b when unordered
//   MaxKeepA(a, b)  a <  b ? b : a    the higher; a when unordered
//
// The two with `<` are for values that are never NaN (a NaN there is a bug upstream: EI_NAN_AUDIT logs it
// in the verify and debug builds); the two with `>=` are the ones the original's masks give a NaN to.

#include <bit>
#include <cstdint>

#include "nan_audit.h"

// The sign bit of `v` (set for -0.0 and for a negative NaN).
inline bool SignBit(float v) { return (std::bit_cast<std::uint32_t>(v) >> 31) != 0; }

// |v| by clearing the sign bit (the bits of a NaN stay).
inline float AbsBits(float v) { return std::bit_cast<float>(std::bit_cast<std::uint32_t>(v) & 0x7FFFFFFFu); }

inline float MinKeepA(float a, float b) { return a >= b ? b : a; }
inline float MaxKeepB(float a, float b) { return a >= b ? a : b; }

inline float MinKeepB(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a < b ? a : b;
}
inline float MaxKeepA(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a < b ? b : a;
}

// `ifLess` when a < b or unordered, else `otherwise` (the original's branch-free select).
inline float SelectLess(float a, float b, float ifLess, float otherwise) { return a >= b ? otherwise : ifLess; }
