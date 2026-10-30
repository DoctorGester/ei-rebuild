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
//
// Write a select of two floats on `>=` or `<=` through these, not as a conditional expression: MSVC compiles
// `a >= b ? a : b` to MAXSS and `a >= b ? b : a` to MINSS, which return the second operand on a tie, so where
// +0 meets -0 the zero's sign would differ from the original's compare and branch. The `>=` helpers are a
// compare's mask and a blend (SelectLess), which keeps the operand the compare picks. `a < b ? a : b` and
// `a < b ? b : a` compile to MINSS / MAXSS exactly.

#include <bit>
#include <cstdint>

#include "nan_audit.h"

// The sign bit of `v` (set for -0.0 and for a negative NaN).
inline bool SignBit(float v) { return (std::bit_cast<std::uint32_t>(v) >> 31) != 0; }

// |v| by clearing the sign bit (the bits of a NaN stay).
inline float AbsBits(float v) { return std::bit_cast<float>(std::bit_cast<std::uint32_t>(v) & 0x7FFFFFFFu); }

// `ifLess` when a < b or unordered, else `otherwise` (the original's branch-free select): a >= b (false when
// unordered) as a mask of all ones, which picks the operand's bits.
inline float SelectLess(float a, float b, float ifLess, float otherwise) {
    const std::uint32_t notLess = 0u - static_cast<std::uint32_t>(a >= b);
    return std::bit_cast<float>((std::bit_cast<std::uint32_t>(otherwise) & notLess) |
                                (std::bit_cast<std::uint32_t>(ifLess) & ~notLess));
}

inline float MinKeepA(float a, float b) { return SelectLess(a, b, a, b); }
inline float MaxKeepB(float a, float b) { return SelectLess(a, b, b, a); }
// Floats only: an int or double pair would convert silently.
template <class A, class B> float MinKeepA(A, B) = delete;
template <class A, class B> float MaxKeepB(A, B) = delete;

inline float MinKeepB(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a < b ? a : b;
}
inline float MaxKeepA(float a, float b) {
    EI_NAN_AUDIT(a, b);
    return a < b ? b : a;
}
