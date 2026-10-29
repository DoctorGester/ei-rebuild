#pragma once

// An x87 add or multiply on the game's main thread, for the code built for SSE2. The main thread runs at 24-bit
// precision, where an add or a multiply rounds the exact result once to a float significand. An operand the x87
// registers hold exactly but a float cannot (an int above 2^24, a double: a wide constant, a double return) makes
// that a different value from float maths, and from double maths then a float conversion: the double can land on
// the midpoint between two floats and the second rounding then goes to the even one. These compute the exact
// result as a double and its error, then round once (docs/notes/cleanup-tooling.md, "The x87 files to SSE2"). For
// results in the normal float range; SSE2 code only (the double maths must round at 53 bits).

#if defined(_M_IX86_FP) && _M_IX86_FP < 2
#error "lib/rounded_ops.h needs the double maths of SSE2"
#endif

#include <bit>
#include <cstdint>

namespace lib {

// hi + lo rounded once to a float (ties to even), where hi is hi + lo rounded to a double (a two-sum's result and
// error). Only a hi on a float midpoint (the 29 bits below a float's significand 1000...0) rounds differently than
// hi + lo: it moves one double step towards lo first.
inline float RoundPair(double hi, double lo) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(hi);
    if ((bits & 0x1FFFFFFFull) == 0x10000000ull && lo != 0.0) {
        const bool away = (lo > 0.0) == (hi > 0.0);
        hi = std::bit_cast<double>(away ? bits + 1 : bits - 1);
    }
    return static_cast<float>(hi);
}

// a + b rounded once to a float (two-sum: the double sum and its exact error).
inline float RoundedSum(double a, double b) {
    const double sum = a + b;
    const double bPart = sum - a;
    const double error = (a - (sum - bPart)) + (b - bPart);
    return RoundPair(sum, error);
}

// a * b rounded once to a float: b split in two 26-bit halves (Veltkamp), each product with a float exact in a
// double, their sum by two-sum.
inline float RoundedProduct(float a, double b) {
    const double scaled = 134217729.0 * b;   // 2^27 + 1
    const double high = scaled - (scaled - b);
    const double low = b - high;
    const double first = a * high;
    const double second = a * low;
    const double sum = first + second;
    const double secondPart = sum - first;
    const double error = (first - (sum - secondPart)) + (second - secondPart);
    return RoundPair(sum, error);
}

} // namespace lib
