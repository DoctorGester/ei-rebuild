#pragma once

// An x87 divide on the game's main thread, for the code built for SSE2 (lib/rounded_ops.h has the add and the
// multiply): a / b rounded once to a float significand, as the x87 divide rounds it at 24-bit precision when an operand is wider
// than a float (an int above 2^24, a double read from text). The double quotient, then its remainder a - q * b
// (exact in sign: q * b by Dekker's two-product, a - q * b's leading part exact as the two are within a factor of
// 2) says which side of a float midpoint the exact quotient lies on. For results in the normal float range; SSE2
// code only.

#include "lib/rounded_ops.h"

namespace lib {

inline float RoundedQuotient(double a, double b) {
    const double quotient = a / b;
    // quotient * b = product + error exactly (both split in two 26-bit halves, Veltkamp).
    auto split = [](double x, double& high, double& low) {
        const double scaled = 134217729.0 * x;   // 2^27 + 1
        high = scaled - (scaled - x);
        low = x - high;
    };
    double qh, ql, bh, bl;
    split(quotient, qh, ql);
    split(b, bh, bl);
    const double product = quotient * b;
    const double error = ((qh * bh - product) + qh * bl + ql * bh) + ql * bl;
    const double remainder = (a - product) - error;
    return RoundPair(quotient, b > 0 ? remainder : -remainder);
}

} // namespace lib
