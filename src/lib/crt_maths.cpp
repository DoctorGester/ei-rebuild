// The game CRT's maths as it computes it on the main thread, whose x87 runs at 24-bit precision: the bodies of
// _CIacos, _CIasin, _CIpow and _hypot (fmod / _CIfmod, modf, floor and ceil are the host CRT's), in SSE2 code. Our
// callers call them by name. Why these stay ported: docs/notes/standalone.md, "The CRT".
#include "crt.h"
#include "pc24.h"
#include "vecmath.h"

#include <bit>
#include <cmath>
#include <cstdint>

namespace libcrt {

namespace {

constexpr std::uint64_t kMantissa = 0x000FFFFFFFFFFFFFull;
constexpr std::uint64_t kQuiet = 0x0008000000000000ull;
constexpr std::uint64_t kSign = 0x8000000000000000ull;
// The x87 real indefinite as a double: the CRT's error result (the long double).
constexpr std::uint64_t kIndefinite = 0xFFF8000000000000ull;
constexpr std::uint64_t kInfinity = 0x7FF0000000000000ull;
// pi and pi / 2 (FLDPI, and the long double) as the callers store them.
constexpr double kHalfPi = 1.5707963267948966;

std::uint64_t Bits(double value) { return std::bit_cast<std::uint64_t>(value); }
double FromBits(std::uint64_t bits) { return std::bit_cast<double>(bits); }

unsigned Exponent(std::uint64_t bits) { return static_cast<unsigned>(bits >> 52) & 0x7FF; }
bool IsNan(std::uint64_t bits) { return Exponent(bits) == 0x7FF && (bits & kMantissa) != 0; }
bool IsInfinite(std::uint64_t bits) { return Exponent(bits) == 0x7FF && (bits & kMantissa) == 0; }
bool IsQuiet(std::uint64_t bits) { return (bits & kQuiet) != 0; }

// An overflowing result as the CRT's error handling returns it (_87except) under rounding to nearest, the main
// thread's: an infinity.
double Overflow(bool negative) { return FromBits(kInfinity | (negative ? kSign : 0)); }

// x + y where one is a NaN, as the x87 adds two doubles (FLD x, FADD y): x is loaded first, which quietens a
// signalling x; then a NaN against a number is the NaN, quiet; a signalling y against the quiet x is x; two quiet ones
// give the larger significand, of equal ones the positive.
double NaNSum(double x, double y) {
    std::uint64_t a = Bits(x);
    const std::uint64_t b = Bits(y);
    if (IsNan(a)) a |= kQuiet;
    if (!IsNan(b)) return FromBits(a);
    if (!IsNan(a)) return FromBits(b | kQuiet);
    if (!IsQuiet(b)) return FromBits(a);
    const std::uint64_t ma = a & kMantissa, mb = b & kMantissa;
    if (ma == mb) return FromBits((a & kSign) ? b : a);
    return FromBits(mb > ma ? b : a);
}

// frexp and ldexp by the bits for normal doubles (both exact, so the same results as std::frexp /
// std::ldexp, which save, set and restore the control word around every call); zeros,
// subnormals, infinities and NaNs, and results outside the normal range, go to them.
constexpr std::uint64_t kExponentBits = 0x7FF0000000000000ull;

double Mantissa(double value, int* exponent) {
    const std::uint64_t bits = Bits(value);
    const unsigned biased = Exponent(bits);
    if (biased == 0 || biased == 0x7FF) return std::frexp(value, exponent);
    *exponent = static_cast<int>(biased) - 1022;
    return FromBits((bits & ~kExponentBits) | (std::uint64_t(1022) << 52));
}

double Scale(double value, int exponent) {
    const std::uint64_t bits = Bits(value);
    const int biased = static_cast<int>(Exponent(bits));
    if (biased != 0 && biased != 0x7FF && exponent > -2046 && exponent < 2046) {
        const int scaled = biased + exponent;
        if (scaled > 0 && scaled < 0x7FF) return FromBits((bits & ~kExponentBits) | (std::uint64_t(scaled) << 52));
    }
    return std::ldexp(value, exponent);
}

// What the CRT's integer test says of y: 0 not an integer, 1 odd, 2 even (from the bits: y * 0.5 must not be
// rounded).
int IntegerKind(double y) {
    const std::uint64_t bits = Bits(y);
    const int exponent = static_cast<int>(Exponent(bits)) - 1023;
    if (exponent < 0) return (bits << 1) == 0 ? 2 : 0;
    if (exponent >= 53) return 2;
    const std::uint64_t significand = (bits & kMantissa) | (kMantissa + 1);
    const int fractionBits = 52 - exponent;
    if (significand & ((std::uint64_t(1) << fractionBits) - 1)) return 0;
    return (significand >> fractionBits) & 1 ? 1 : 2;
}

// The same test as the CRT runs it on a zero or negative x: y * 0.5 is rounded to 24 bits first, so an odd y beyond
// 2^24 tests even.
int IntegerKindAt24(double y) {
    if (IntegerKind(y) == 0) return 0;
    const float half = static_cast<float>(y * 0.5);
    return std::floor(half) == half ? 2 : 1;
}

// _CIacos / _CIasin: |x| < 1 by FPATAN of the root of (1 + x)(1 - x) and x, the sum, the difference, the product
// and the root each rounded to 24 bits, FPATAN's 64-bit result stored to a double; +-1 exactly; NaN quietened;
// anything else the indefinite (with a domain error the game never reads).
double ArcCosineOrSine(double x, bool sine) {
    const std::uint64_t bits = Bits(x);
    const unsigned exponent = Exponent(bits);
    if (exponent == 0x7FF) return IsNan(bits) ? FromBits(bits | kQuiet) : FromBits(kIndefinite);
    if (exponent > 0x3FF || (exponent == 0x3FF && (bits & kMantissa) != 0)) return FromBits(kIndefinite);
    if (exponent == 0x3FF) {   // +-1
        if (sine) return bits & kSign ? -kHalfPi : kHalfPi;
        return bits & kSign ? kPiDouble : 0.0;
    }
    const float sum = pc24::Add(1.0, x);
    const float difference = pc24::Sub(1.0, x);
    const float root = std::sqrt(sum * difference);
    // FPATAN's register stored as a double is hi, the 64-bit value rounded to 53 bits (hi + lo here would be an x87
    // add into the return register, at the thread's precision).
    return (sine ? pc24::Atan2(x, root) : pc24::Atan2(root, x)).hi;
}

// _powhlp for an infinite x or y (neither a NaN): the C limits; |x| == 1 to an infinite power is
// the indefinite.
double PowerOfInfinity(double x, double y) {
    const double size = std::fabs(x);
    const double infinity = FromBits(kInfinity);
    const double negativeZero = FromBits(kSign);
    if (IsInfinite(Bits(y))) {
        if (size == 1.0) return FromBits(kIndefinite);
        return (size > 1.0) == (y > 0.0) ? infinity : 0.0;
    }
    if (y == 0.0) return 1.0;
    if (x > 0.0) return y > 0.0 ? infinity : 0.0;
    const bool odd = IntegerKind(y) == 1;
    if (y > 0.0) return odd ? -infinity : infinity;
    return odd ? negativeZero : 0.0;
}

// ---- _hypot's steps: x87 operations at 64-bit precision, each result stored to a double ----------------------------
// A result rounded to 64 bits and then to 53 is the double nearest the exact value but where the 64-bit rounding
// lands on the middle between two doubles: then the even one. With hi the nearest double and the exact value
// hi + lo, that is hi but where hi is odd and |lo| is at least 2^-12 ulps short of half an ulp (the 64-bit grid's
// half step; on it, the 64-bit rounding ties to the middle, whose 64-bit significand is even).

// hi's ulp times (1/2 - 2^-12) for a positive normal hi: the size of lo from which the double rounding moves.
double MovingSize(double hi) { return FromBits((Bits(hi) & kExponentBits) - (std::uint64_t(52) << 52)) * 0x1.ffcp-2; }

// The double next to a positive normal hi toward lo's side, when hi is odd and |lo| reaches MovingSize; else hi.
double Settle(double hi, bool odd, bool moves, bool up) {
    if (!odd || !moves) return hi;
    return FromBits(Bits(hi) + (up ? 1 : ~std::uint64_t{0}));
}

// a * b exactly as p + e (Dekker's two-product, for operands well inside the double range).
struct Product {
    double p, e;
};
Product TwoProduct(double a, double b) {
    const auto split = [](double v, double& high, double& low) {
        const double t = 134217729.0 * v;
        high = t - (t - v);
        low = v - high;
    };
    const double p = a * b;
    double ah, al, bh, bl;
    split(a, ah, al);
    split(b, bh, bl);
    return {p, ((ah * bh - p) + ah * bl + al * bh) + al * bl};
}

// a * b for positive normal doubles near 1 (FMUL at 64 bits, FSTP qword).
double StoredProduct(double a, double b) {
    const Product x = TwoProduct(a, b);
    return Settle(x.p, Bits(x.p) & 1, std::fabs(x.e) >= MovingSize(x.p), x.e > 0.0);
}

// a / b for 1 <= b < 2, 2^-34 <= a <= b (FDIV at 64 bits, FSTP qword). The remainder a - q b is a double, exactly
// (q * b's two parts; a - p is exact as p is near a): the quotient's lo is it over b, so lo's size is compared as
// |r| against b * MovingSize(q), which has 64 bits (kh + kl exactly).
double StoredQuotient(double a, double b) {
    const double q = a / b;
    const Product x = TwoProduct(q, b);
    const double r = (a - x.p) - x.e;
    const Product k = TwoProduct(b, MovingSize(q));
    const double size = std::fabs(r);
    const bool moves = size >= 0.5 * k.p && size - k.p >= k.e;
    return Settle(q, Bits(q) & 1, moves, r > 0.0);
}

// The 128-bit product of two 64-bit integers.
struct Wide {
    std::uint64_t hi, lo;
};
Wide WideProduct(std::uint64_t a, std::uint64_t b) {
    const std::uint64_t a0 = static_cast<std::uint32_t>(a), a1 = a >> 32;
    const std::uint64_t b0 = static_cast<std::uint32_t>(b), b1 = b >> 32;
    const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const std::uint64_t middle = (p00 >> 32) + static_cast<std::uint32_t>(p01) + static_cast<std::uint32_t>(p10);
    return {p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32), (middle << 32) | static_cast<std::uint32_t>(p00)};
}

// value >> shift rounded to the nearest integer, ties to even; `sticky`: nonzero bits below value.
std::uint64_t ShiftRounded(std::uint64_t value, int shift, bool sticky) {
    if (shift == 0) return value;
    if (shift > 64) return 0;
    const std::uint64_t kept = shift == 64 ? 0 : value >> shift;
    const std::uint64_t half = std::uint64_t(1) << (shift - 1);
    const std::uint64_t rest = shift == 64 ? value : value & ((half << 1) - 1);
    if (rest > half || (rest == half && (sticky || (kept & 1)))) return kept + 1;
    return kept;
}

// 1 + t^2 for 2^-34 <= t <= 1: FMUL t, t and FADD of 1 (1 * 1) at 64 bits, FSTP qword. t^2 is the square of t's
// 53-bit significand (106 bits at most) rounded to 64 bits, then added to 1 on the 2^-63 grid, then rounded to 53 bits.
double StoredOnePlusSquare(double t) {
    const std::uint64_t bits = Bits(t);
    const std::uint64_t significand = (bits & kMantissa) | (kMantissa + 1);   // t = significand 2^(e - 52)
    const int e = static_cast<int>(Exponent(bits)) - 1023;
    const Wide square = WideProduct(significand, significand);                 // t^2 = square 2^(2e - 104)
    const int drop = (square.hi >> 41) ? 42 : 41;                              // 106 or 105 bits long
    std::uint64_t square64 = (square.hi << (64 - drop)) | (square.lo >> drop);
    const std::uint64_t dropped = square.lo & ((std::uint64_t(1) << drop) - 1);
    const std::uint64_t half = std::uint64_t(1) << (drop - 1);
    if (dropped > half || (dropped == half && (square64 & 1))) ++square64;     // never all ones: no carry out
    // t^2 rounded is square64 2^(2e - 104 + drop), at most 1: in units of 2^-63, rounded.
    const std::uint64_t fraction = ShiftRounded(square64, 104 - drop - 63 - 2 * e, false);
    if (fraction >> 63) return 2.0;   // t is 1
    return static_cast<double>(ShiftRounded((std::uint64_t(1) << 63) | fraction, 11, false)) * 0x1p-52;
}

// sqrt(v) for 1 <= v <= 2 (FSQRT at 64 bits, FSTP qword). The root's 64 bits are the integer square root of
// N = v 2^126, rounded: the double root s plus its correction (v - s^2) / 2s, both in units of 2^-63, is within one
// of it, and the remainder N - q^2 settles it.
double StoredRoot(double v) {
    const double s = std::sqrt(v);
    const Product sq = TwoProduct(s, s);
    const double correction = ((v - sq.p) - sq.e) / (s + s) * 0x1p63;
    std::uint64_t q = static_cast<std::uint64_t>(s * 0x1p63) + static_cast<std::int64_t>(std::floor(correction));
    const Wide n{static_cast<std::uint64_t>(v * 0x1p52) << 10, 0};   // v 2^52 is at most 2^53
    const auto less = [](Wide a, Wide b) { return a.hi < b.hi || (a.hi == b.hi && a.lo < b.lo); };
    while (less(n, WideProduct(q, q))) --q;
    while (!less(n, WideProduct(q + 1, q + 1))) ++q;
    // To nearest: up when N - q^2 > q (sqrt(N) above q + 1/2; never on it, N being an integer).
    Wide upper = WideProduct(q, q);
    upper.lo += q;
    if (upper.lo < q) ++upper.hi;
    if (less(upper, n)) ++q;
    return static_cast<double>(ShiftRounded(q, 11, false)) * 0x1p-52;
}

} // namespace

double CiAcos(double x) { return ArcCosineOrSine(x, false); }
double CiAsin(double x) { return ArcCosineOrSine(x, true); }

// x^y: y log2(x) by FYL2X, then 2 to that as 2^n times the rounded 1 + (2^f - 1), f = t - n, n
// the nearest integer (F2XM1, FSCALE). A negative x needs an integer y (the result negated for an odd
// one), a zero x gives 1, a zero or an infinity; NaNs propagate, infinities as C has them.
double CiPow(double x, double y) {
    const std::uint64_t xBits = Bits(x);
    const std::uint64_t yBits = Bits(y);
    // The operands come on the x87 stack, both loaded (quiet): FADDP of the two.
    if (IsNan(xBits) || IsNan(yBits)) return NaNSum(x, FromBits(IsNan(yBits) ? yBits | kQuiet : yBits));
    if (IsInfinite(xBits) || IsInfinite(yBits)) return PowerOfInfinity(x, y);
    if (x == 0.0) {
        if (Exponent(yBits) == 0) return (yBits & kMantissa) ? 0.0 : 1.0;
        const bool odd = (xBits & kSign) && IntegerKindAt24(y) == 1;
        if (y < 0.0) return odd ? -FromBits(kInfinity) : FromBits(kInfinity);
        return odd ? FromBits(kSign) : 0.0;
    }
    bool negate = false;
    if (x < 0.0) {
        const int kind = IntegerKindAt24(y);
        if (kind == 0) return FromBits(kIndefinite);
        negate = kind == 1;
        x = -x;
    }
    const pc24::Unrounded exponent = pc24::Log2Times(y, x);
    if (std::fabs(exponent.hi) >= 4096.0) {   // far out of the double range (FYL2X itself does not overflow); never NaN
        if (exponent.hi > 0.0) return Overflow(negate);
        return negate ? FromBits(kSign) : 0.0;
    }
    const pc24::PowerOfTwo power = pc24::TwoToThe(exponent);
    const double result = Scale(power.factor, power.exponent);
    if (IsInfinite(Bits(result))) return Overflow(negate);
    return negate ? -result : result;
}

// _hypothlp's steps under its control word 133F (64 bits, to nearest), each result stored to a double as the x87
// code stores it (rounded twice: the steps above): the smaller of |x| and |y| over the larger (the larger's own
// quotient is 1), the sum of the squares, its root, then the root's and the larger's mantissas (_decomp: [0.5, 1))
// multiplied and given the exponents' sum (_set_exp, exact while the result is a normal double; below that, the
// masked underflow's denormalised result, cut off).
double Hypot(double x, double y) {
    const std::uint64_t xBits = Bits(x);
    const std::uint64_t yBits = Bits(y);
    if (Exponent(xBits) == 0x7FF || Exponent(yBits) == 0x7FF) {
        if (IsNan(xBits) || IsNan(yBits)) return NaNSum(x, y);
        return FromBits(kInfinity);
    }
    const double sizeX = std::fabs(x);
    const double sizeY = std::fabs(y);
    const double larger = sizeY > sizeX ? sizeY : sizeX;
    const double smaller = sizeY > sizeX ? sizeX : sizeY;
    if (larger == 0.0) return 0.0;
    // Both scaled by the larger's power of two (exact; a smaller one that would leave the normal range is below
    // 2^-34 of the larger): the quotient the same, the scaled larger in [1, 2).
    int largerExponent = 0;
    const double largerMantissa = Mantissa(larger, &largerExponent);
    const double scaledLarger = largerMantissa * 2.0;
    const double quotient = smaller / larger;
    double sum = 1.0;   // the smaller's square below 2^-68: 1 + it rounds to 1 at 64 bits
    if (quotient >= 0x1p-34) sum = StoredOnePlusSquare(StoredQuotient(Scale(smaller, 1 - largerExponent), scaledLarger));
    const double root = StoredRoot(sum);
    int rootExponent = 0;
    const double rootMantissa = Mantissa(root, &rootExponent);
    const double product = StoredProduct(rootMantissa, largerMantissa);
    int productExponent = 0;
    const double productMantissa = Mantissa(product, &productExponent);
    const int exponent = productExponent + rootExponent + largerExponent;   // result = mantissa 2^exponent
    if (exponent < -1021) {
        // Below the normal range: the masked underflow's denormalised result, the 53-bit significand
        // shifted down with the dropped bits cut off (not rounded).
        const std::uint64_t significand = static_cast<std::uint64_t>(Scale(productMantissa, 53));
        const int shift = -1021 - exponent;
        return FromBits(shift >= 64 ? 0 : significand >> shift);
    }
    const double result = Scale(product, rootExponent + largerExponent);
    if (IsInfinite(Bits(result))) return Overflow(false);
    return result;
}

} // namespace libcrt
