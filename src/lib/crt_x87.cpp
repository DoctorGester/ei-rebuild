// The game CRT's x87 entry points with register arguments (S4c, docs/notes/standalone.md): _ftol (ST0 in, its
// 64-bit truncation in EDX:EAX, ST0 popped) and the _CI maths (_CIacos, _CIasin: ST0 in and out; _CIpow,
// _CIfmod: ST1 and ST0 in, the result in ST0). A C++ function cannot be declared with that ABI, so each is a
// naked entry that moves its arguments to the stack and calls a plain C++ body; this file is the one in
// src/lib that tools/check_cleanup.py lets hold __asm, for these entries only.
//
// Registered for the standalone image only (EI_REPLACE_STANDALONE): the hook build keeps the exe's own, so
// its x87 results (and the goldens recorded with them) stay the original's. tests/verify/verify_crt_x87.cpp
// compares every entry with the original at start-up.
#include "crt.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace libcrt {

namespace {

constexpr std::uint64_t kMantissa = 0x000FFFFFFFFFFFFFull;
constexpr std::uint64_t kQuiet = 0x0008000000000000ull;
constexpr std::uint64_t kSign = 0x8000000000000000ull;
// The x87 real indefinite as a double: the CRT's error result (the long double at 00797B30).
constexpr std::uint64_t kIndefinite = 0xFFF8000000000000ull;
constexpr std::uint64_t kInfinity = 0x7FF0000000000000ull;
// pi and pi / 2 (FLDPI, and the long double at 00797B3A) as the callers store them.
constexpr double kPi = 3.141592653589793;
constexpr double kHalfPi = 1.5707963267948966;

std::uint64_t Bits(double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

double FromBits(std::uint64_t bits) {
    double value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

unsigned Exponent(std::uint64_t bits) { return static_cast<unsigned>(bits >> 52) & 0x7FF; }
bool IsNan(std::uint64_t bits) { return Exponent(bits) == 0x7FF && (bits & kMantissa) != 0; }
bool IsInfinite(std::uint64_t bits) { return Exponent(bits) == 0x7FF && (bits & kMantissa) == 0; }

// The control word the _CI functions compute under (006EF665): rounding to nearest, every exception
// masked, the caller's precision kept, so their FADD / FSUB / FMUL / FSQRT round to the caller's
// precision (24 bits on the game's main thread). The bodies compute at 53 bits and round with Round where
// the original's instructions round. (FYL2X, F2XM1, FPATAN and FPREM ignore the precision.)
// (_hypot computes at 64 bits: `precision`.)
class MathControlWord {
public:
    explicit MathControlWord(unsigned precision = _PC_53) {
        __control87_2(0, 0, &saved_, nullptr);
        single_ = (saved_ & _MCW_PC) == _PC_24;
        rounding_ = saved_ & _MCW_RC;
        unsigned ignored;
        __control87_2(_RC_NEAR | precision | _MCW_EM, _MCW_RC | _MCW_PC | _MCW_EM, &ignored, nullptr);
    }
    ~MathControlWord() {
        unsigned ignored;
        __control87_2(saved_, _MCW_RC | _MCW_PC | _MCW_EM, &ignored, nullptr);
    }
    MathControlWord(const MathControlWord&) = delete;
    MathControlWord& operator=(const MathControlWord&) = delete;

    // A result of an x87 add, subtract, multiply or square root at the caller's precision.
    double Round(double value) const {
        if (!single_) return value;
        volatile float stored = static_cast<float>(value);
        return stored;
    }

    // An overflowing result as the CRT's error handling returns it (_87except): infinity or the largest
    // double, as the caller's rounding direction has it.
    double Overflow(bool negative) const {
        const double infinity = FromBits(kInfinity);
        const double largest = DBL_MAX;
        double size = infinity;
        if (rounding_ == _RC_CHOP || (rounding_ == _RC_UP && negative) || (rounding_ == _RC_DOWN && !negative))
            size = largest;
        return negative ? -size : size;
    }

private:
    unsigned saved_ = 0;
    unsigned rounding_ = _RC_NEAR;
    bool single_ = false;
};

// What the CRT's integer test (006E8395) says of y: 0 not an integer, 1 odd, 2 even (from the bits: the
// caller's precision must not round y * 0.5).
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

// The same test as the CRT runs it on a zero or negative x, under the caller's precision: y * 0.5 is
// rounded first, so at 24 bits an odd y beyond 2^24 tests even.
int IntegerKindAt(double y, const MathControlWord& control) {
    if (IntegerKind(y) == 0) return 0;
    const double half = control.Round(y * 0.5);
    return std::floor(half) == half ? 2 : 1;
}

// FRNDINT under rounding to nearest: ties to even.
double RoundToEven(double value) {
    const double down = std::floor(value);
    const double rest = value - down;
    if (rest > 0.5) return down + 1.0;
    if (rest < 0.5) return down;
    return IntegerKind(down) == 1 ? down + 1.0 : down;
}

// _CIacos / _CIasin (006E733D / 006E83DD): |x| < 1 by FPATAN of the root of (1 + x)(1 - x) and x; +-1
// exactly; NaN quietened; anything else the indefinite (with a domain error the game never reads).
double ArcCosineOrSine(double x, bool sine) {
    const std::uint64_t bits = Bits(x);
    const unsigned exponent = Exponent(bits);
    if (exponent == 0x7FF) return IsNan(bits) ? FromBits(bits | kQuiet) : FromBits(kIndefinite);
    if (exponent > 0x3FF || (exponent == 0x3FF && (bits & kMantissa) != 0)) return FromBits(kIndefinite);
    if (exponent == 0x3FF) {   // +-1
        if (sine) return bits & kSign ? -kHalfPi : kHalfPi;
        return bits & kSign ? kPi : 0.0;
    }
    const MathControlWord control;
    const double sum = control.Round(1.0 + x);
    const double difference = control.Round(1.0 - x);
    const double root = control.Round(x87::fsqrt(control.Round(sum * difference)));
    return sine ? x87::fpatan(x, root) : x87::fpatan(root, x);
}

// _powhlp (006F1527) for an infinite x or y (neither a NaN): the C limits; |x| == 1 to an infinite power is
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

} // namespace

std::int64_t __cdecl FtolExtended(const unsigned char* value) {
    std::uint64_t mantissa;
    std::uint16_t signAndExponent;
    std::memcpy(&mantissa, value, sizeof mantissa);
    std::memcpy(&signAndExponent, value + 8, sizeof signAndExponent);
    constexpr std::int64_t kIntegerIndefinite = INT64_MIN;
    const int exponent = signAndExponent & 0x7FFF;
    if (exponent == 0x7FFF) return kIntegerIndefinite;   // infinity, NaN
    const int whole = exponent - 16383;                  // the value is mantissa * 2^(whole - 63)
    if (whole < 0) return 0;
    if (whole >= 63) return kIntegerIndefinite;          // |value| >= 2^63 (-2^63 has the same bits)
    const std::int64_t truncated = static_cast<std::int64_t>(mantissa >> (63 - whole));
    return signAndExponent & 0x8000 ? -truncated : truncated;
}

double __cdecl CiAcos(double x) { return ArcCosineOrSine(x, false); }
double __cdecl CiAsin(double x) { return ArcCosineOrSine(x, true); }

// x mod y as FPREM leaves it (exact, the sign of x; a zero x gives +0); the indefinite for a zero or
// infinite y or an infinite x (the CRT's operand-class table, 00797630), NaNs as x87 addition propagates
// them.
double __cdecl CiFmod(double x, double y) {
    const std::uint64_t xBits = Bits(x);
    const std::uint64_t yBits = Bits(y);
    if (IsNan(xBits) || IsNan(yBits)) return x + y;
    if (y == 0.0 || IsInfinite(xBits) || IsInfinite(yBits)) return FromBits(kIndefinite);
    if (x == 0.0) return 0.0;   // +0 for -0 too
    return std::fmod(x, y);
}

// x^y (006E81C2): y log2(x) by FYL2X, then 2 to that as 2^n times the rounded 1 + (2^f - 1), f = t - n, n
// the nearest integer (F2XM1, FSCALE). A negative x needs an integer y (the result negated for an odd
// one), a zero x gives 1, a zero or an infinity; NaNs propagate, infinities as C has them.
double __cdecl CiPow(double x, double y) {
    const std::uint64_t xBits = Bits(x);
    const std::uint64_t yBits = Bits(y);
    if (IsNan(xBits) || IsNan(yBits)) return x + y;
    if (IsInfinite(xBits) || IsInfinite(yBits)) return PowerOfInfinity(x, y);
    const MathControlWord control;
    if (x == 0.0) {
        if (Exponent(yBits) == 0) return (yBits & kMantissa) ? 0.0 : 1.0;
        const bool odd = (xBits & kSign) && IntegerKindAt(y, control) == 1;
        if (y < 0.0) return odd ? -FromBits(kInfinity) : FromBits(kInfinity);
        return odd ? FromBits(kSign) : 0.0;
    }
    bool negate = false;
    if (x < 0.0) {
        const int kind = IntegerKindAt(y, control);
        if (kind == 0) return FromBits(kIndefinite);
        negate = kind == 1;
        x = -x;
    }
    // t = y log2(x), FYL2X's 64-bit product, as y k + y log2(m) for x = m 2^k, m in [sqrt(1/2), sqrt(2)),
    // with the products' rounding errors kept apart: t - n then keeps the low bits a double t would lose.
    int k = 0;
    double m = std::frexp(x, &k);
    if (m < 0.70710678118654752) {
        m *= 2.0;
        --k;
    }
    const double logM = std::log2(m);
    const double kPart = y * k;
    const double kError = std::fma(y, k, -kPart);
    const double mPart = y * logM;
    const double mError = std::fma(y, logM, -mPart);
    const double exponent = kPart + mPart;
    if (!(std::fabs(exponent) < 4096.0)) {   // far out of the double range (FYL2X itself does not overflow)
        if (exponent > 0.0) return control.Overflow(negate);
        return negate ? FromBits(kSign) : 0.0;
    }
    const double whole = RoundToEven(exponent);
    const double fraction = control.Round(((kPart - whole) + mPart) + (kError + mError));
    const double power = std::ldexp(control.Round(std::exp2(fraction)), static_cast<int>(whole));
    if (IsInfinite(Bits(power))) return control.Overflow(negate);
    return negate ? -power : power;
}

namespace {

// An 80-bit value as fmod's argument loader (006F2DAE) builds it from a double: FLD for a finite one;
// for an infinity or a NaN the exponent all ones and the mantissa's high dword the double's shifted
// left 11 (with the low dword's top bits), but its low dword the double's own, unshifted (the
// loader leaves out the SHL): a NaN's payload comes out changed.
void LoadAsFmod(double value, unsigned char* to) {
    const std::uint64_t bits = Bits(value);
    if (Exponent(bits) != 0x7FF) {
        __asm {
            fld value
            mov eax, to
            fstp tbyte ptr [eax]
        }
        return;
    }
    const std::uint32_t high = static_cast<std::uint32_t>(bits >> 32);
    const std::uint32_t low = static_cast<std::uint32_t>(bits);
    const std::uint32_t mantissaHigh = (high << 11) | (low >> 21);
    const std::uint16_t signAndExponent = static_cast<std::uint16_t>((high >> 16) | 0x7FFF);
    std::memcpy(to, &low, 4);
    std::memcpy(to + 4, &mantissaHigh, 4);
    std::memcpy(to + 8, &signAndExponent, 2);
}

// x + y of two 80-bit values on the x87 (a NaN's propagation as FADD has it).
__declspec(naked) double __cdecl AddExtended(const unsigned char* /*x*/, const unsigned char* /*y*/) {
    __asm {
        mov eax, [esp + 4]
        fld tbyte ptr [eax]
        mov eax, [esp + 8]
        fld tbyte ptr [eax]
        faddp st(1), st
        ret
    }
}

} // namespace

double __cdecl StackFmod(double x, double y) {
    if (!IsNan(Bits(x)) && !IsNan(Bits(y))) return CiFmod(x, y);
    unsigned char loadedX[10];
    unsigned char loadedY[10];
    LoadAsFmod(x, loadedX);
    LoadAsFmod(y, loadedY);
    return AddExtended(loadedX, loadedY);
}

namespace {

// x + y of two doubles on the x87 (FLD, FADD: a NaN's propagation as the CRT's _handle_qnan2 has it).
__declspec(naked) double __cdecl AddOnX87(double /*x*/, double /*y*/) {
    __asm {
        fld qword ptr [esp + 4]
        fadd qword ptr [esp + 12]
        ret
    }
}

} // namespace

// _hypothlp's steps under its control word 133F (64 bits, to nearest), each result stored to a double as
// the original stores it (so rounded twice: volatile keeps the stores): |x| and |y| over the larger, the
// sum of their squares, its root, then the root's and the larger's mantissas (_decomp: [0.5, 1))
// multiplied and given the exponents' sum (_set_exp, exact while the result is a normal double; below
// that, the masked underflow's denormalised result, cut off).
double __cdecl Hypot(double x, double y) {
    const std::uint64_t xBits = Bits(x);
    const std::uint64_t yBits = Bits(y);
    if (Exponent(xBits) == 0x7FF || Exponent(yBits) == 0x7FF) {
        if (IsNan(xBits) || IsNan(yBits)) return AddOnX87(x, y);
        return FromBits(kInfinity);
    }
    const double sizeX = std::fabs(x);
    const double sizeY = std::fabs(y);
    const double larger = sizeY > sizeX ? sizeY : sizeX;
    if (larger == 0.0) return 0.0;
    const MathControlWord control(_PC_64);
    const volatile double a = sizeX / larger;
    const volatile double b = sizeY / larger;
    const volatile double sum = a * a + b * b;
    const volatile double root = x87::fsqrt(sum);
    int rootExponent = 0;
    int largerExponent = 0;
    const double rootMantissa = std::frexp(root, &rootExponent);
    const volatile double product = rootMantissa * std::frexp(larger, &largerExponent);
    int productExponent = 0;
    const double productMantissa = std::frexp(product, &productExponent);
    const int exponent = productExponent + rootExponent + largerExponent;   // result = mantissa 2^exponent
    if (exponent < -1021) {
        // Below the normal range: the masked underflow's denormalised result, the 53-bit significand
        // shifted down with the dropped bits cut off (not rounded).
        const std::uint64_t significand = static_cast<std::uint64_t>(std::ldexp(productMantissa, 53));
        const int shift = -1021 - exponent;
        return FromBits(shift >= 64 ? 0 : significand >> shift);
    }
    const double result = std::ldexp(product, rootExponent + largerExponent);
    if (IsInfinite(Bits(result))) return control.Overflow(false);
    return result;
}

// ---- The register entries ----

__declspec(naked) std::int64_t __cdecl FtolEntry() {
    __asm {
        push ecx
        sub esp, 12
        fstp tbyte ptr [esp]
        push esp
        call FtolExtended
        add esp, 16
        pop ecx
        ret
    }
}
EI_REPLACE_STANDALONE(0x006E853C, libcrt::FtolEntry);

__declspec(naked) double __cdecl CiAcosEntry() {
    __asm {
        push ecx
        push edx
        sub esp, 8
        fstp qword ptr [esp]
        call CiAcos
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}
EI_REPLACE_STANDALONE(0x006E7320, libcrt::CiAcosEntry);

__declspec(naked) double __cdecl CiAsinEntry() {
    __asm {
        push ecx
        push edx
        sub esp, 8
        fstp qword ptr [esp]
        call CiAsin
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}
EI_REPLACE_STANDALONE(0x006E83C0, libcrt::CiAsinEntry);

// acos, asin and fmod with their arguments on the stack (cdecl, the result in ST0): the same bodies
// (S16: the terrain's wave step and ambient angle, the global map and the weather sway call them).
EI_REPLACE_STANDALONE(0x006E7334, libcrt::CiAcos);
EI_REPLACE_STANDALONE(0x006E83D4, libcrt::CiAsin);
EI_REPLACE_STANDALONE(0x006E95A0, libcrt::StackFmod);
EI_REPLACE_STANDALONE(0x006FD026, libcrt::Hypot);   // S16a

// ST1 = x, ST0 = y.
__declspec(naked) double __cdecl CiPowEntry() {
    __asm {
        push ecx
        push edx
        sub esp, 16
        fstp qword ptr [esp + 8]
        fstp qword ptr [esp]
        call CiPow
        add esp, 16
        pop edx
        pop ecx
        ret
    }
}
EI_REPLACE_STANDALONE(0x006E81A0, libcrt::CiPowEntry);

__declspec(naked) double __cdecl CiFmodEntry() {
    __asm {
        push ecx
        push edx
        sub esp, 16
        fstp qword ptr [esp + 8]
        fstp qword ptr [esp]
        call CiFmod
        add esp, 16
        pop edx
        pop ecx
        ret
    }
}
EI_REPLACE_STANDALONE(0x006E95AA, libcrt::CiFmodEntry);

} // namespace libcrt
