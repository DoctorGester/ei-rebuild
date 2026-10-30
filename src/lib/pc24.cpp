// pc24.h's helpers: double-double arithmetic on SSE2 (hi + lo, about 106 bits), the transcendentals' series, and the
// one rounding to a 24-bit significand. The constants are exact hex splits made with Python's Decimal and Fraction
// (a one-off script); the series' heads are double-double and their tails, below 2^-18 of the
// result, plain double.
#include "pc24.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>

#if defined(_M_IX86) && (!defined(_M_IX86_FP) || _M_IX86_FP < 2)
#error "pc24.cpp needs SSE2 doubles: on the main thread's 24-bit x87 a double rounds to a float's 24 bits"
#endif

namespace pc24 {

namespace {

using DD = Unrounded;

// ln 2, pi and pi / 2.
constexpr DD kLn2{0x1.62e42fefa39efp-1, 0x1.abc9e3b39803fp-56};
constexpr DD kPi{0x1.921fb54442d18p+1, 0x1.1a62633145c07p-53};
constexpr DD kHalfPi{0x1.921fb54442d18p+0, 0x1.1a62633145c07p-54};
// FLDLN2, FLDLG2 and FLDL2E: ln 2, log10 2 and log2 e rounded to 64 bits (exact as hi + lo).
constexpr DD kLn2x87{0x1.62e42fefa39efp-1, 0x1.ac00000000000p-56};
constexpr DD kLg2x87{0x1.34413509f79ffp-2, -0x1.9c00000000000p-59};
constexpr DD kL2Ex87{0x1.71547652b82fep+0, 0x1.7800000000000p-56};
// FLDLN2 / ln 2 and FLDLG2 / ln 2: ln m to FYL2X's ln 2 * log2 m and log10 2 * log2 m.
constexpr DD kLnScale{0x1.0000000000000p+0, 0x1.384291b5ee917p-66};
constexpr DD kLgScale{0x1.bcb7b1526e50ep-2, 0x1.95d79bcb584bcp-57};

// The FPU's quarter turn for FSIN / FCOS: half of its 66-bit pi 0xC90FDAA22168C234C * 2^-66, kQuarterTurn * 2^-65
// (66 bits, kQuarterTurnHigh the top 2).
constexpr std::uint64_t kQuarterTurnHigh = 0x3;
constexpr std::uint64_t kQuarterTurnLow = 0x243f6a8885a308d3ull;
constexpr double kQuarterTurn = 0x1.921fb54442d18p+0;   // rounded, for the quotient's estimate
constexpr double kTwoOverPi = 0x1.45f306dc9c883p-1;

// sin r = r + r z S(z), z = r^2: -1/3!, 1/5!, -1/7!, 1/9!, then -1/11! .. 1/27!.
constexpr DD kSinHead[] = {
    {-0x1.5555555555555p-3, -0x1.5555555555555p-57},
    {0x1.1111111111111p-7, 0x1.1111111111111p-63},
    {-0x1.a01a01a01a01ap-13, -0x1.a01a01a01a01ap-73},
    {0x1.71de3a556c734p-19, -0x1.c154f8ddc6c00p-73},
};
constexpr double kSinTail[] = {-0x1.ae64567f544e4p-26, 0x1.6124613a86d09p-33, -0x1.ae7f3e733b81fp-41,
                               0x1.952c77030ad4ap-49,  -0x1.2f49b46814157p-57, 0x1.71b8ef6dcf572p-66,
                               -0x1.761b41316381ap-75, 0x1.3f3ccdd165fa9p-84,  -0x1.d1ab1c2dccea3p-94};
// cos r = 1 + z C(z): -1/2!, 1/4!, -1/6!, 1/8!, then -1/10! .. 1/26!.
constexpr DD kCosHead[] = {
    {-0x1.0000000000000p-1, 0x0.0p+0},
    {0x1.5555555555555p-5, 0x1.5555555555555p-59},
    {-0x1.6c16c16c16c17p-10, 0x1.f49f49f49f49fp-65},
    {0x1.a01a01a01a01ap-16, 0x1.a01a01a01a01ap-76},
};
constexpr double kCosTail[] = {-0x1.27e4fb7789f5cp-22, 0x1.1eed8eff8d898p-29, -0x1.93974a8c07c9dp-37,
                               0x1.ae7f3e733b81fp-45,  -0x1.6827863b97d97p-53, 0x1.e542ba4020225p-62,
                               -0x1.0ce396db7f853p-70, 0x1.f2cf01972f578p-80,  -0x1.88e85fc6a4e5ap-89};
// atan u = u + u z A(z), |u| <= 1/32: -1/3, 1/5, -1/7, then 1/9 .. 1/21.
constexpr DD kAtanHead[] = {
    {-0x1.5555555555555p-2, -0x1.5555555555555p-56},
    {0x1.999999999999ap-3, -0x1.999999999999ap-57},
    {-0x1.2492492492492p-3, -0x1.2492492492492p-57},
};
constexpr double kAtanTail[] = {0x1.c71c71c71c71cp-4,  -0x1.745d1745d1746p-4, 0x1.3b13b13b13b14p-4,
                                -0x1.1111111111111p-4, 0x1.e1e1e1e1e1e1ep-5,  -0x1.af286bca1af28p-5,
                                0x1.8618618618618p-5};
// atan(j / 16), j = 0 .. 16.
constexpr DD kAtanTable[] = {
    {0x0.0p+0, 0x0.0p+0},
    {0x1.ff55bb72cfdeap-5, -0x1.c934d86d23f1dp-60},
    {0x1.fd5ba9aac2f6ep-4, -0x1.cd37686760c17p-59},
    {0x1.7b97b4bce5b02p-3, 0x1.347b0b4f881cap-58},
    {0x1.f5b75f92c80ddp-3, 0x1.8ab6e3cf7afbdp-57},
    {0x1.362773707ebccp-2, -0x1.963a544b672d8p-57},
    {0x1.6f61941e4def1p-2, -0x1.c63aae6f6e918p-56},
    {0x1.a64eec3cc23fdp-2, -0x1.24dec1b50b7ffp-56},
    {0x1.dac670561bb4fp-2, 0x1.a2b7f222f65e2p-56},
    {0x1.0657e94db30d0p-1, -0x1.d5b495f6349e6p-56},
    {0x1.1e00babdefeb4p-1, -0x1.928df287a668fp-58},
    {0x1.345f01cce37bbp-1, 0x1.1021137c71102p-55},
    {0x1.4978fa3269ee1p-1, 0x1.2419a87f2a458p-56},
    {0x1.5d58987169b18p-1, 0x1.0028e4bc5e7cap-57},
    {0x1.700a7c5784634p-1, -0x1.8c34d25aadef6p-56},
    {0x1.819d0b7158a4dp-1, -0x1.bf76229d3b917p-56},
    {0x1.921fb54442d18p-1, 0x1.1a62633145c07p-55},
};
// ln m = 2s + 2s z L(z), s = (m - 1) / (m + 1), |s| <= 0.172: 1/3, 1/5, 1/7, then 1/9 .. 1/41.
constexpr DD kLogHead[] = {
    {0x1.5555555555555p-2, 0x1.5555555555555p-56},
    {0x1.999999999999ap-3, -0x1.999999999999ap-57},
    {0x1.2492492492492p-3, 0x1.2492492492492p-57},
};
constexpr double kLogTail[] = {0x1.c71c71c71c71cp-4, 0x1.745d1745d1746p-4, 0x1.3b13b13b13b14p-4, 0x1.1111111111111p-4,
                               0x1.e1e1e1e1e1e1ep-5, 0x1.af286bca1af28p-5, 0x1.8618618618618p-5, 0x1.642c8590b2164p-5,
                               0x1.47ae147ae147bp-5, 0x1.2f684bda12f68p-5, 0x1.1a7b9611a7b96p-5, 0x1.0842108421084p-5,
                               0x1.f07c1f07c1f08p-6, 0x1.d41d41d41d41dp-6, 0x1.bacf914c1bad0p-6, 0x1.a41a41a41a41ap-6,
                               0x1.8f9c18f9c18fap-6};
// e^w = 1 + w + w^2 E(w), |w| <= 0.35: 1/2! .. 1/6!, then 1/7! .. 1/20!.
constexpr DD kExpHead[] = {
    {0x1.0000000000000p-1, 0x0.0p+0},
    {0x1.5555555555555p-3, 0x1.5555555555555p-57},
    {0x1.5555555555555p-5, 0x1.5555555555555p-59},
    {0x1.1111111111111p-7, 0x1.1111111111111p-63},
    {0x1.6c16c16c16c17p-10, -0x1.f49f49f49f49fp-65},
};
constexpr double kExpTail[] = {0x1.a01a01a01a01ap-13, 0x1.a01a01a01a01ap-16, 0x1.71de3a556c734p-19,
                               0x1.27e4fb7789f5cp-22, 0x1.ae64567f544e4p-26, 0x1.1eed8eff8d898p-29,
                               0x1.6124613a86d09p-33, 0x1.93974a8c07c9dp-37, 0x1.ae7f3e733b81fp-41,
                               0x1.ae7f3e733b81fp-45, 0x1.952c77030ad4ap-49, 0x1.6827863b97d97p-53,
                               0x1.2f49b46814157p-57, 0x1.e542ba4020225p-62};

// The FPU's default NaN (an invalid operation's result: FSIN of an infinity, FYL2X of a negative).
const double kIndefinite = std::bit_cast<double>(0xFFF8000000000000ull);

// --- Double-double arithmetic ----------------------------------------------------------------------------------

// a + b exactly as s + e (Knuth's two-sum).
DD TwoSum(double a, double b) {
    const double s = a + b;
    const double bb = s - a;
    return {s, (a - (s - bb)) + (b - bb)};
}

// The same when |a| >= |b| or a is 0 (Dekker).
DD FastTwoSum(double a, double b) {
    const double s = a + b;
    return {s, b - (s - a)};
}

// a as high + low, high with 26 significant bits (Veltkamp).
void Split(double a, double& high, double& low) {
    const double t = 134217729.0 * a;
    high = t - (t - a);
    low = a - high;
}

// a * b exactly as p + e (Dekker's two-product: no FMA on this target).
DD TwoProd(double a, double b) {
    const double p = a * b;
    double ah, al, bh, bl;
    Split(a, ah, al);
    Split(b, bh, bl);
    return {p, ((ah * bh - p) + ah * bl + al * bh) + al * bl};
}

DD Neg(DD a) { return {-a.hi, -a.lo}; }

DD AddDD(DD a, DD b) {
    DD s = TwoSum(a.hi, b.hi);
    const DD t = TwoSum(a.lo, b.lo);
    s = FastTwoSum(s.hi, s.lo + t.hi);
    return FastTwoSum(s.hi, s.lo + t.lo);
}

DD MulDD(DD a, DD b) {
    const DD p = TwoProd(a.hi, b.hi);
    return FastTwoSum(p.hi, p.lo + (a.hi * b.lo + a.lo * b.hi));
}

// Three quotient digits; each remainder is exact when a and b are doubles, so a quotient on a rounding tie stays on it.
DD DivDD(DD a, DD b) {
    const double q1 = a.hi / b.hi;
    DD r = AddDD(a, Neg(MulDD(b, q1)));
    const double q2 = r.hi / b.hi;
    r = AddDD(r, Neg(MulDD(b, q2)));
    const double q3 = r.hi / b.hi;
    return AddDD(FastTwoSum(q1, q2), q3);
}

// head[0] + z (head[1] + .. z (head[n-1] + z tail(z))): the tail in double on z's high part.
template <std::size_t Heads, std::size_t Tails>
DD Series(DD z, const DD (&head)[Heads], const double (&tail)[Tails]) {
    double t = tail[Tails - 1];
    for (std::size_t i = Tails - 1; i-- > 0;) t = tail[i] + z.hi * t;
    DD sum = t;
    for (std::size_t i = Heads; i-- > 0;) sum = AddDD(head[i], MulDD(z, sum));
    return sum;
}

// An x87 operation with a NaN: the NaN, quiet; of two, the larger significand, of equal ones the positive.
double NaNOf(double a, double b) {
    if (!std::isnan(a)) return b;
    if (!std::isnan(b)) return a;
    const std::uint64_t ma = std::bit_cast<std::uint64_t>(a) & 0x000FFFFFFFFFFFFFull;
    const std::uint64_t mb = std::bit_cast<std::uint64_t>(b) & 0x000FFFFFFFFFFFFFull;
    if (ma == mb) return std::signbit(a) ? b : a;
    return mb > ma ? b : a;
}

// --- The rounding ----------------------------------------------------------------------------------------------

// The value rounded once to a 24-bit significand, ties to even, the exponent kept (an x87 register at PC24). hi is
// first rounded to odd at 53 bits, using lo's sign (the sticky bit), which makes the second rounding exact. Zeros,
// infinities and NaNs come back as hi.
double Round24(DD v) {
    if (v.hi == 0.0 || !std::isfinite(v.hi)) return v.hi;
    std::uint64_t bits = std::bit_cast<std::uint64_t>(v.hi);
    if (v.lo != 0.0 && (bits & 1) == 0) bits += (v.lo > 0.0) == (v.hi > 0.0) ? 1 : ~std::uint64_t{0};
    bits += 0x0FFFFFFFu + ((bits >> 29) & 1);
    return std::bit_cast<double>(bits & ~std::uint64_t{0x1FFFFFFF});
}

// The value rounded to a 64-bit significand, ties to even: the FPU's register after FSIN, FCOS, FPATAN, FYL2X or
// F2XM1. It matters where the true value is within 2^-64 of a short one: FSIN of a small x is x itself, and x times a
// float can be a tie the next operation breaks to even.
DD Round64(DD v) {
    if (v.hi == 0.0 || !std::isfinite(v.hi)) return v;
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(v.hi);
    double unit = std::bit_cast<double>(((bits >> 52 & 0x7FF) - 63) << 52);   // hi's ulp / 2^11
    // A power of two with lo toward zero: the value is in the binade below.
    if ((bits & 0x000FFFFFFFFFFFFFull) == 0 && v.lo != 0.0 && (v.lo < 0.0) != (v.hi < 0.0)) unit *= 0.5;
    // lo in units (exact, at most 2^11), to the nearest, ties to even: hi's part of the significand is even.
    const double units = v.lo / unit;
    return FastTwoSum(v.hi, ((units + 0x1.8p52) - 0x1.8p52) * unit);
}

// A register stored as a float (FSTP dword): one rounding, into the denormals too. hi rounded to odd at 53 bits keeps
// lo's side for the cast's rounding.
float StoreRegister(DD v) {
    if (v.hi == 0.0 || !std::isfinite(v.hi)) return static_cast<float>(v.hi);
    std::uint64_t bits = std::bit_cast<std::uint64_t>(v.hi);
    if (v.lo != 0.0 && (bits & 1) == 0) bits += (v.lo > 0.0) == (v.hi > 0.0) ? 1 : ~std::uint64_t{0};
    return static_cast<float>(std::bit_cast<double>(bits));
}

// An operation's float (rounded at PC24, then stored): `plain` is it on the high parts, which gives the IEEE
// infinities, NaNs and the sign of an exact 0; of two NaNs the FPU's.
float Store(DD exact, double plain, DD a, DD b) {
    if (std::isnan(a.hi) && std::isnan(b.hi)) return static_cast<float>(NaNOf(a.hi, b.hi));
    if (!std::isfinite(plain)) return static_cast<float>(plain);
    // A double operand above about 2^996 overflows the splitting: such a result is beyond a float's range.
    if (!std::isfinite(exact.hi)) return static_cast<float>(plain);
    if (exact.hi == 0.0) return static_cast<float>(plain == 0.0 ? plain : 0.0);
    return static_cast<float>(Round24(exact));
}

// --- 128-bit integers for FSIN's reduction ---------------------------------------------------------------------

struct U128 {
    std::uint64_t lo;
    std::uint64_t hi;
};

U128 Mul64(std::uint64_t a, std::uint64_t b) {
    const std::uint64_t a0 = static_cast<std::uint32_t>(a), a1 = a >> 32;
    const std::uint64_t b0 = static_cast<std::uint32_t>(b), b1 = b >> 32;
    const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const std::uint64_t middle = (p00 >> 32) + static_cast<std::uint32_t>(p01) + static_cast<std::uint32_t>(p10);
    return {(middle << 32) | static_cast<std::uint32_t>(p00), p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32)};
}

U128 Sub128(U128 a, U128 b) { return {a.lo - b.lo, a.hi - b.hi - (a.lo < b.lo ? 1u : 0u)}; }

// q times the quarter turn's 66-bit integer (mod 2^128).
U128 TimesQuarterTurn(std::uint64_t q) {
    U128 p = Mul64(q, kQuarterTurnLow);
    p.hi += q * kQuarterTurnHigh;
    return p;
}

// A signed 128-bit value below 2^100 in magnitude, times 2^-65, as a double-double.
DD ToDD(U128 v) {
    const bool negative = (v.hi >> 63) != 0;
    if (negative) v = Sub128({0, 0}, v);
    const double top = static_cast<double>((v.hi << 28) | (v.lo >> 36)) * 0x1p36;   // bits 36 .. 99
    const double bottom = static_cast<double>(v.lo & ((std::uint64_t{1} << 36) - 1));
    DD d = TwoSum(top * 0x1p-65, bottom * 0x1p-65);
    return negative ? Neg(d) : d;
}

// |x| = q * P66/2 + r exactly, |r| <= P66/4 (and a hair), for 0 < |x| < 2^63; FSIN / FCOS take sin / cos of r by q's
// quadrant. The quotient's estimate in double is up to 2^10 off for the largest x: the remainder corrects it once.
// x is a float or a double: the 128-bit integers hold either's multiples exactly.
struct Reduced {
    DD r;
    unsigned quadrant;
};

Reduced Reduce(double ax) {
    std::uint64_t q = static_cast<std::uint64_t>(ax * kTwoOverPi + 0.5);
    if (q == 0) return {DD{ax}, 0};
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(ax);
    const std::uint64_t mantissa = (bits & 0x000FFFFFFFFFFFFFull) | 0x0010000000000000ull;   // ax >= 0.78: normal
    const int shift = static_cast<int>(bits >> 52) - 1075 + 65;   // ax = mantissa * 2^(shift - 65), 12 .. 75
    const U128 scaled = shift >= 64 ? U128{0, mantissa << (shift - 64)}
                                    : U128{mantissa << shift, mantissa >> (64 - shift)};
    U128 rest = Sub128(scaled, TimesQuarterTurn(q));
    DD r = ToDD(rest);
    const double fix = std::floor(r.hi / kQuarterTurn + 0.5);
    if (fix != 0.0) {
        const std::uint64_t steps = static_cast<std::uint64_t>(std::fabs(fix));
        if (fix > 0.0) {
            q += steps;
            rest = Sub128(rest, TimesQuarterTurn(steps));
        } else {
            q -= steps;
            const U128 back = TimesQuarterTurn(steps);
            rest = Sub128(rest, Sub128({0, 0}, back));
        }
        r = ToDD(rest);
    }
    return {r, static_cast<unsigned>(q & 3)};
}

DD SinOf(DD r) {
    const DD z = MulDD(r, r);
    return AddDD(r, MulDD(MulDD(r, z), Series(z, kSinHead, kSinTail)));
}

DD CosOf(DD r) {
    const DD z = MulDD(r, r);
    return AddDD(1.0, MulDD(z, Series(z, kCosHead, kCosTail)));
}

// sin (cosine false) or cos of a float or a double by FSIN / FCOS's reduction.
Unrounded SinCos(double x, bool cosine) {
    if (std::isnan(x)) return x;
    if (std::isinf(x)) return kIndefinite;
    const double ax = std::fabs(x);
    if (ax >= 0x1p63) return x;
    const Reduced reduced = Reduce(ax);
    const unsigned quadrant = (reduced.quadrant + (cosine ? 1 : 0)) & 3;
    const DD v = Round64((quadrant & 1) ? CosOf(reduced.r) : SinOf(reduced.r));
    // sin's sign: the quadrant's, and x's (odd); cos is even.
    const bool negative = ((quadrant & 2) != 0) != (!cosine && std::signbit(x));
    return negative ? Neg(v) : v;
}

// atan t for 0 <= t <= 1: atan(j/16) + atan((t - j/16) / (1 + t j/16)).
DD AtanUnit(DD t) {
    const int j = static_cast<int>(t.hi * 16.0 + 0.5);
    const double c = j * 0.0625;
    const DD u = DivDD(AddDD(t, -c), AddDD(1.0, MulDD(t, c)));
    const DD z = MulDD(u, u);
    return AddDD(kAtanTable[j], AddDD(u, MulDD(MulDD(u, z), Series(z, kAtanHead, kAtanTail))));
}

// ln x scaled: e * intScale + ln m * mScale, x = m 2^e with sqrt(1/2) <= m < sqrt(2).
Unrounded LogScaled(float x, DD intScale, DD mScale) {
    if (std::isnan(x)) return static_cast<double>(x);
    if (x == 0.0f) return -HUGE_VAL;
    if (x < 0.0f) return kIndefinite;
    if (std::isinf(x)) return HUGE_VAL;
    std::uint32_t bits = std::bit_cast<std::uint32_t>(x);
    int exponent = static_cast<int>(bits >> 23);
    std::uint32_t mantissa = bits & 0x7FFFFFu;
    if (exponent == 0) {   // a denormal: normalise
        const int shift = std::countl_zero(mantissa) - 8;
        mantissa = (mantissa << shift) & 0x7FFFFFu;
        exponent = 1 - shift;
    }
    double m = std::bit_cast<float>(mantissa | 0x3F800000u);
    int e = exponent - 127;
    if (m > 1.4142135) {
        m *= 0.5;
        ++e;
    }
    const DD s = DivDD(m - 1.0, m + 1.0);
    const DD z = MulDD(s, s);
    const DD lnM = MulDD(AddDD(s, MulDD(MulDD(s, z), Series(z, kLogHead, kLogTail))), 2.0);
    return Round64(AddDD(MulDD(static_cast<double>(e), intScale), MulDD(lnM, mScale)));
}

// e^w - 1, |w| <= 0.35.
DD ExpMinusOne(DD w) { return AddDD(w, MulDD(MulDD(w, w), Series(w, kExpHead, kExpTail))); }

// g * 2^n for an integer n (FSCALE), in steps that stay exact while the result is a normal double.
double Scale(double g, double n) {
    if (std::isnan(g) || g == 0.0) return g;
    int k = static_cast<int>(n < -2200.0 ? -2200.0 : (n > 2200.0 ? 2200.0 : n));
    for (; k > 1000; k -= 1000) g *= 0x1p1000;
    for (; k < -1000; k += 1000) g *= 0x1p-1000;
    return g * std::bit_cast<double>(static_cast<std::uint64_t>(1023 + k) << 52);
}

} // namespace

Unrounded Sin(float x) { return SinCos(x, false); }
Unrounded Cos(float x) { return SinCos(x, true); }
Unrounded Sin(double x) { return SinCos(x, false); }
Unrounded Cos(double x) { return SinCos(x, true); }

Unrounded Atan2(float y, float x) {
    if (std::isnan(y) || std::isnan(x)) return NaNOf(y, x);
    const double ay = std::fabs(y), ax = std::fabs(x);
    DD angle;
    if (std::isinf(ay) && std::isinf(ax)) angle = kAtanTable[16];
    else if (ay > ax) angle = AddDD(kHalfPi, Neg(AtanUnit(std::isinf(ay) ? DD{0.0} : DivDD(ax, ay))));
    else angle = AtanUnit(ay == 0.0 || std::isinf(ax) ? DD{0.0} : DivDD(ay, ax));
    if (std::signbit(x)) angle = AddDD(kPi, Neg(angle));
    angle = Round64(angle);
    return std::signbit(y) ? Neg(angle) : angle;
}

Unrounded Log(float x) { return LogScaled(x, kLn2x87, kLnScale); }
Unrounded Log10(float x) { return LogScaled(x, kLg2x87, kLgScale); }

float Exp(float x) {
    // FLD x, FLDL2E, FMULP: rounded at PC24.
    const DD product = MulDD(static_cast<double>(x), kL2Ex87);
    const double t = std::isfinite(product.hi) ? Round24(product) : product.hi;
    // FRNDINT (ties to even; from 2^51 up t is an integer), FSUB: the fraction, exact.
    const double n = std::fabs(t) < 0x1p51 ? (t + 0x1.8p52) - 0x1.8p52 : t;
    const double f = t - n;
    // F2XM1, FLD1, FADDP: 2^f rounded at PC24.
    const double g = Round24(AddDD(1.0, Round64(ExpMinusOne(MulDD(f, kLn2)))));
    // FSCALE, then the store.
    return static_cast<float>(Scale(g, n));
}

float Round(Unrounded value) { return StoreRegister(value); }
float Add(Unrounded a, Unrounded b) { return Store(AddDD(a, b), a.hi + b.hi, a, b); }
float Sub(Unrounded a, Unrounded b) { return Store(AddDD(a, Neg(b)), a.hi - b.hi, a, b); }
float Mul(Unrounded a, Unrounded b) { return Store(MulDD(a, b), a.hi * b.hi, a, b); }
float Div(Unrounded a, Unrounded b) {
    const double plain = a.hi / b.hi;
    if (b.hi == 0.0 || !std::isfinite(b.hi)) return Store(DD{plain}, plain, a, b);
    return Store(DivDD(a, b), plain, a, b);
}

} // namespace pc24
