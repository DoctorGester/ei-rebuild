// The game CRT's formatted text (S4a): MSVC 6's _output behind sprintf / vsprintf / fprintf / printf
// and its _input number parser behind atof, with the CRT's own number conversion: $I10_OUTPUT's 17
// digits from a 12-byte long double (96-bit mantissa products, the exe's power-of-ten tables), _fptostr's
// rounding of that digit string, and __strgtold12 / _ld12cvt back. All integer arithmetic, so the
// results are the same bytes as the original's in either build. docs/notes/standalone.md, S4a.
#include "crt.h"

#include "ei/hook.h"
#include "game/originals_library.h"
#include "gameapi.h"

#include <climits>
#include <cstdint>
#include <cstring>
#include <vector>

#ifndef EI_VECTORS_REPLAY
namespace libcrt {

namespace {

// ---- The CRT data the conversions read (the same addresses as crt.cpp's) ----

// The decimal point of the LC_NUMERIC locale (__decimal_point[0], 00797FE8).
char DecimalPoint() { return *reinterpret_cast<const char*>(0x00797FE8); }
// _pctype (00797DD8): the class word of a byte; a negative char indexes the bytes before.
unsigned short CharClass(int c) { return (*reinterpret_cast<const unsigned short* const*>(0x00797DD8))[c]; }
unsigned LocaleHandle() { return *reinterpret_cast<const unsigned*>(0x007CABF0); }   // __lc_handle[LC_CTYPE]
unsigned LocaleCodePage() { return *reinterpret_cast<const unsigned*>(0x007CAC00); } // __lc_codepage
int MbCurMax() { return *reinterpret_cast<const int*>(0x00797FE4); }                  // __mb_cur_max
constexpr unsigned short kDigit = 0x4;
constexpr unsigned short kSpace = 0x8;
constexpr unsigned short kLeadByte = 0x8000;

// errno = EILSEQ (wctomb): the game CRT's per-thread errno in the hook build (_getptd + 8); the image's
// callers never read it.
void SetIllegalSequence() {
    static const bool standalone = ei::Standalone();
    if (!standalone) static_cast<int*>(CrtThreadDataOriginal())[2] = 42;
}

// ---- The 12-byte long double (_LDBL12): a 96-bit number as six 16-bit words, lowest first ----
// As a long double: w[0] the extra low word, w[1..2] the low mantissa dword, w[3..4] the high one
// (bit 79 the integer bit), w[5] sign and exponent (bias 0x3FFF). As a plain 96-bit number (the
// multiply's sum, the digit generator): the dwords at w[0], w[2], w[4].

struct Ld12 {
    std::uint16_t w[6];
};

std::uint32_t Get32(const Ld12& x, int i) { return x.w[i] | static_cast<std::uint32_t>(x.w[i + 1]) << 16; }
void Set32(Ld12& x, int i, std::uint32_t v) {
    x.w[i] = static_cast<std::uint16_t>(v);
    x.w[i + 1] = static_cast<std::uint16_t>(v >> 16);
}

// __shl_12 / __shr_12: the 96 bits one place.
void ShiftLeft(Ld12& x) {
    const std::uint32_t lo = Get32(x, 0), med = Get32(x, 2), hi = Get32(x, 4);
    Set32(x, 0, lo << 1);
    Set32(x, 2, med << 1 | lo >> 31);
    Set32(x, 4, hi << 1 | med >> 31);
}
void ShiftRight(Ld12& x) {
    const std::uint32_t lo = Get32(x, 0), med = Get32(x, 2), hi = Get32(x, 4);
    Set32(x, 0, lo >> 1 | med << 31);
    Set32(x, 2, med >> 1 | hi << 31);
    Set32(x, 4, hi >> 1);
}
// ___add_12: x += y modulo 2^96.
void Add(Ld12& x, const Ld12& y) {
    std::uint64_t sum = static_cast<std::uint64_t>(Get32(x, 0)) + Get32(y, 0);
    Set32(x, 0, static_cast<std::uint32_t>(sum));
    sum = (sum >> 32) + Get32(x, 2) + Get32(y, 2);
    Set32(x, 2, static_cast<std::uint32_t>(sum));
    sum = (sum >> 32) + Get32(x, 4) + Get32(y, 4);
    Set32(x, 4, static_cast<std::uint32_t>(sum));
}

void PutZero(Ld12& x) { x = Ld12{}; }
void PutInfinity(Ld12& x, std::uint16_t sign) { x = Ld12{{0, 0, 0, 0, 0x8000, static_cast<std::uint16_t>(0x7FFF | sign)}}; }
// ISZERO_12: the mantissa and exponent, not the sign.
bool IsZero(const Ld12& x) { return (Get32(x, 4) & 0x7FFFFFFF) == 0 && Get32(x, 2) == 0 && Get32(x, 0) == 0; }

// __ld12mul (006FACA6): x *= y. Only the partial products of the upper 96 bits are summed, a carry out
// of a 32-bit sum goes into the next 16-bit word only, then normalise, round to the 80 bits above the
// low word (half up when above half or odd), flush to zero / infinity.
void Multiply(Ld12& x, const Ld12& y) {
    std::uint16_t expX = x.w[5];
    std::uint16_t expY = y.w[5];
    const std::uint16_t sign = (expX ^ expY) & 0x8000;
    expX &= 0x7FFF;
    expY &= 0x7FFF;
    std::uint16_t sum = static_cast<std::uint16_t>(expX + expY);
    if (expX >= 0x7FFF || expY >= 0x7FFF || sum > 0xBFFD) {
        PutInfinity(x, sign);
        return;
    }
    if (sum <= 0x3FBF) {
        PutZero(x);
        return;
    }
    if (expX == 0) {
        ++sum;
        if (IsZero(x)) {
            x.w[5] = 0;
            return;
        }
    }
    if (expY == 0) {
        ++sum;
        if (IsZero(y)) {
            PutZero(x);
            return;
        }
    }
    Ld12 t{};
    for (int i = 0; i < 5; ++i) {
        int p = i;
        int q = 4;
        for (int j = 5 - i; j > 0; --j, ++p, --q) {
            const std::uint32_t product = static_cast<std::uint32_t>(x.w[p]) * y.w[q];
            const std::uint32_t before = Get32(t, i);
            const std::uint32_t after = before + product;
            Set32(t, i, after);
            if ((after < before || after < product) && i + 2 < 6) ++t.w[i + 2];
        }
    }
    sum = static_cast<std::uint16_t>(sum - 0x3FFE);
    while (static_cast<std::int16_t>(sum) > 0 && !(t.w[5] & 0x8000)) {
        ShiftLeft(t);
        --sum;
    }
    if (static_cast<std::int16_t>(sum) <= 0) {
        --sum;
        int sticky = 0;
        while (static_cast<std::int16_t>(sum) < 0) {
            if (t.w[0] & 1) ++sticky;
            ShiftRight(t);
            ++sum;
        }
        if (sticky) t.w[0] |= 1;
    }
    if (t.w[0] > 0x8000 || (Get32(t, 0) & 0x1FFFF) == 0x18000) {
        if (Get32(t, 1) == 0xFFFFFFFF) {
            Set32(t, 1, 0);
            if (Get32(t, 3) == 0xFFFFFFFF) {
                Set32(t, 3, 0);
                if (t.w[5] == 0xFFFF) {
                    t.w[5] = 0x8000;
                    ++sum;
                } else {
                    ++t.w[5];
                }
            } else {
                Set32(t, 3, Get32(t, 3) + 1);
            }
        } else {
            Set32(t, 1, Get32(t, 1) + 1);
        }
    }
    if (sum >= 0x7FFF) {
        PutInfinity(x, sign);
        return;
    }
    x.w[0] = t.w[1];
    Set32(x, 1, Get32(t, 2));
    Set32(x, 3, Get32(t, 4));
    x.w[5] = static_cast<std::uint16_t>(sum | sign);
}

// _pow10pos / _pow10neg (0079AE60 / 0079AFC0): 10^1..10^7, 10^8..10^56 by 8, 10^64..10^448 by 64,
// 10^512..10^3584 by 512, 10^4096 (and their reciprocals).
constexpr Ld12 kPow10Positive[29] = {
    {{0x0000, 0x0000, 0x0000, 0x0000, 0xA000, 0x4002}}, {{0x0000, 0x0000, 0x0000, 0x0000, 0xC800, 0x4005}},
    {{0x0000, 0x0000, 0x0000, 0x0000, 0xFA00, 0x4008}}, {{0x0000, 0x0000, 0x0000, 0x0000, 0x9C40, 0x400C}},
    {{0x0000, 0x0000, 0x0000, 0x0000, 0xC350, 0x400F}}, {{0x0000, 0x0000, 0x0000, 0x0000, 0xF424, 0x4012}},
    {{0x0000, 0x0000, 0x0000, 0x8000, 0x9896, 0x4016}}, {{0x0000, 0x0000, 0x0000, 0x2000, 0xBEBC, 0x4019}},
    {{0x0000, 0x0000, 0x0400, 0xC9BF, 0x8E1B, 0x4034}}, {{0x0000, 0xA100, 0xCCED, 0x1BCE, 0xD3C2, 0x404E}},
    {{0xF020, 0xB59E, 0x2B70, 0xADA8, 0x9DC5, 0x4069}}, {{0x5DD0, 0x25FD, 0x1AE5, 0x4F8E, 0xEB19, 0x4083}},
    {{0x9671, 0x95D7, 0x0E43, 0x8D05, 0xAF29, 0x409E}}, {{0xBFF9, 0x44A0, 0x81ED, 0x8F12, 0x8281, 0x40B9}},
    {{0x3CBF, 0xA6D5, 0xFFCF, 0x1F49, 0xC278, 0x40D3}}, {{0xC66F, 0x8CE0, 0x80E9, 0x47C9, 0x93BA, 0x41A8}},
    {{0x85BC, 0x556B, 0x3927, 0xF78D, 0xE070, 0x427C}}, {{0xDDBC, 0xDE8E, 0x9DF9, 0xEBFB, 0xAA7E, 0x4351}},
    {{0xE6A1, 0xE376, 0xF2CC, 0x2F29, 0x8184, 0x4426}}, {{0x1028, 0xAA17, 0xAEF8, 0xE310, 0xC4C5, 0x44FA}},
    {{0xA7EB, 0xF3D4, 0xEBF7, 0x4AE1, 0x957A, 0x45CF}}, {{0xCC65, 0x91C7, 0xA60E, 0xA0AE, 0xE319, 0x46A3}},
    {{0x650D, 0x0C17, 0x8175, 0x7586, 0xC976, 0x4D48}}, {{0x4258, 0xA7E4, 0x3993, 0x353B, 0xB2B8, 0x53ED}},
    {{0xA74D, 0x5DE5, 0xC53D, 0x3B5D, 0x9E8B, 0x5A92}}, {{0x5DFF, 0xF0A6, 0x20A1, 0x54C0, 0x8CA5, 0x6137}},
    {{0xFDD1, 0x5A8B, 0xD88B, 0x5D25, 0xF989, 0x67DB}}, {{0x95AA, 0xF3F8, 0xBF27, 0xC8A2, 0xDD5D, 0x6E80}},
    {{0xC94C, 0x979B, 0x8A20, 0x5202, 0xC460, 0x7525}},
};
constexpr Ld12 kPow10Negative[29] = {
    {{0xCCCD, 0xCCCD, 0xCCCC, 0xCCCC, 0xCCCC, 0x3FFB}}, {{0x3D71, 0xD70A, 0x70A3, 0x0A3D, 0xA3D7, 0x3FF8}},
    {{0x645A, 0xDF3B, 0x8D4F, 0x6E97, 0x8312, 0x3FF5}}, {{0xD3C3, 0x652C, 0xE219, 0x1758, 0xD1B7, 0x3FF1}},
    {{0x0FD0, 0x8423, 0x1B47, 0xAC47, 0xA7C5, 0x3FEE}}, {{0xA640, 0x69B6, 0xAF6C, 0xBD05, 0x8637, 0x3FEB}},
    {{0x3D33, 0x42BC, 0xE57A, 0x94D5, 0xD6BF, 0x3FE7}}, {{0xFDC2, 0xCEFD, 0x8461, 0x7711, 0xABCC, 0x3FE4}},
    {{0x4C2F, 0xE15B, 0xC44D, 0x94BE, 0xE695, 0x3FC9}}, {{0xC492, 0x3B53, 0x4475, 0x14CD, 0x9ABE, 0x3FAF}},
    {{0x67DE, 0x94BA, 0x4539, 0x1EAD, 0xCFB1, 0x3F94}}, {{0x2324, 0xE2C6, 0xBABC, 0x313B, 0x8B61, 0x3F7A}},
    {{0x5561, 0xC159, 0xB17E, 0x7C53, 0xBB12, 0x3F5F}}, {{0xEED7, 0x8D2F, 0xBE06, 0x8592, 0xFB15, 0x3F44}},
    {{0x3F24, 0xE9A5, 0xA539, 0xEA27, 0xA87F, 0x3F2A}}, {{0xAC7D, 0xE4A1, 0x64BC, 0x467C, 0xDDD0, 0x3E55}},
    {{0x7B63, 0xCC06, 0x5423, 0x8377, 0x91FF, 0x3D81}}, {{0xFA91, 0x193A, 0x637A, 0x4325, 0xC031, 0x3CAC}},
    {{0x8921, 0x38D1, 0x4782, 0xB897, 0xFD00, 0x3BD7}}, {{0x88DC, 0x0858, 0xB11B, 0xE3E8, 0xA686, 0x3B03}},
    {{0x84C6, 0x4245, 0xB607, 0x7599, 0xDB37, 0x3A2E}}, {{0x7133, 0xD21C, 0xDB23, 0xEE32, 0x9049, 0x395A}},
    {{0x87A6, 0xC0BE, 0xDA57, 0x82A5, 0xA2A6, 0x32B5}}, {{0x68E2, 0x11B2, 0x52A7, 0x449F, 0xB759, 0x2C10}},
    {{0x4925, 0x2DE4, 0x3436, 0x534F, 0xCEAE, 0x256B}}, {{0x598F, 0xA404, 0xDEC0, 0x7DC2, 0xE8FB, 0x1EC6}},
    {{0xE79E, 0x5A88, 0x9157, 0xBF3C, 0x8350, 0x1822}}, {{0x4B4E, 0x6265, 0x83FD, 0xAF8F, 0x9406, 0x117D}},
    {{0x2DE4, 0x9FDE, 0xD2CE, 0x04C8, 0xA6DD, 0x0AD8}},
};

// __multtenpow12 (006FAEC6): x *= 10^power, three bits of the power per table row. A table entry whose
// extra word is at or above half is used one unit lower in its low mantissa dword ("unrounded").
// Without `mult12` the extra word of x is cleared first.
void MultiplyByPowerOfTen(Ld12& x, int power, bool mult12) {
    if (power == 0) return;
    const Ld12* table = kPow10Positive;
    if (power < 0) {
        power = -power;
        table = kPow10Negative;
    }
    if (!mult12) x.w[0] = 0;
    int row = -8;
    while (power != 0) {
        row += 7;
        const int last3 = power & 7;
        power >>= 3;
        if (last3 == 0) continue;
        Ld12 factor = table[row + last3];
        if (factor.w[0] >= 0x8000) Set32(factor, 1, Get32(factor, 1) - 1);
        Multiply(x, factor);
    }
}

// ---- Double to digits ----

// FOS, $I10_OUTPUT's result: the decimal exponent, the sign (' ' or '-'), the digit count and digits.
struct DigitString {
    std::int16_t exponent;
    char sign;
    char length;
    char digits[24];
};

void PutZeroDigits(DigitString& out) {
    out.exponent = 0;
    out.sign = ' ';
    out.length = 1;
    out.digits[0] = '0';
    out.digits[1] = '\0';
}

// $I10_OUTPUT (006FAA13) on an 80-bit value (the low and high mantissa dwords, sign and exponent):
// scale into [0.1, 1) by a power of ten estimated low from the exponent and the mantissa's top byte,
// generate `count` + 1 digits (at most 22) by multiplying the 96-bit fraction by ten, and round on the
// last one; trailing zeros are dropped. With `fixed` (flag 1) `count` counts from the decimal point.
// Returns 0 for infinity and NaN (written as "1#INF", "1#IND", "1#QNAN", "1#SNAN"), else 1.
int TenByteToDigits(std::uint32_t low, std::uint32_t high, std::uint16_t exponentWord, int count, bool fixed,
                    DigitString& out) {
    const Ld12 oneTenth = {{0xCCCC, 0xCCCC, 0xCCCC, 0xCCCC, 0xCCCC, 0x3FFB}};
    const std::uint16_t sign = exponentWord & 0x8000;
    const std::uint16_t exponent = exponentWord & 0x7FFF;
    out.sign = sign ? '-' : ' ';
    if (exponent == 0 && high == 0 && low == 0) {
        PutZeroDigits(out);
        return 1;
    }
    if (exponent == 0x7FFF) {
        out.exponent = 1;
        const char* text;
        if (!(high == 0x80000000 && low == 0) && !(high & 0x40000000)) {
            text = "1#SNAN";
        } else if (sign && high == 0xC0000000) {
            text = low == 0 ? "1#IND" : "1#QNAN";
        } else {
            text = high == 0x80000000 && low == 0 ? "1#INF" : "1#QNAN";
        }
        std::memcpy(out.digits, text, std::strlen(text) + 1);
        out.length = static_cast<char>(std::strlen(text));
        return 0;
    }
    Ld12 value{};
    Set32(value, 1, low);
    Set32(value, 3, high);
    value.w[5] = exponent;
    const std::uint32_t estimate = ((exponent >> 8) + (high >> 24) * 2) * 0x4Du + exponent * 0x4D10u + 0xECBCED0Cu;
    std::int16_t decimal = static_cast<std::int16_t>(static_cast<std::int32_t>(estimate) >> 16);
    MultiplyByPowerOfTen(value, -decimal, true);
    if (value.w[5] >= 0x3FFF) {
        ++decimal;
        Multiply(value, oneTenth);
    }
    out.exponent = decimal;
    if (fixed) {
        count += decimal;
        if (count <= 0) {
            PutZeroDigits(out);
            return 1;
        }
    }
    if (count > 21) count = 21;
    const int shift = value.w[5] - 0x3FFE;
    value.w[5] = 0;
    for (int i = 0; i < 8; ++i) ShiftLeft(value);
    if (shift < 0) {
        for (unsigned n = static_cast<unsigned>(-shift) & 0xFF; n != 0; --n) ShiftRight(value);
    }
    char* p = out.digits;
    for (int n = count + 1; n > 0; --n) {
        const Ld12 copy = value;
        ShiftLeft(value);
        ShiftLeft(value);
        Add(value, copy);
        ShiftLeft(value);
        *p++ = static_cast<char>((value.w[5] >> 8) + '0');
        value.w[5] &= 0x00FF;
    }
    const char last = p[-1];
    char* q = p - 2;
    if (last >= '5') {
        while (q >= out.digits && *q == '9') *q-- = '0';
        if (q < out.digits) {
            ++q;
            ++out.exponent;
        }
        ++*q;
    } else {
        while (q >= out.digits && *q == '0') --q;
        if (q < out.digits) {
            PutZeroDigits(out);
            return 1;
        }
    }
    out.length = static_cast<char>(q - out.digits + 1);
    out.digits[static_cast<int>(out.length)] = '\0';
    return 1;
}

// STRFLT: the sign, the decimal exponent (digits before the point), the digits.
struct Decimal {
    int sign;
    int point;
    char* digits;
};

// _fltout2 (006F9584): __dtold to 80 bits (a zero of either sign becomes +0; a denormal is
// normalised), then 17 digits.
Decimal ToDecimal(double value, char* digits) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof bits);
    const std::uint32_t lo = static_cast<std::uint32_t>(bits);
    const std::uint32_t hi = static_cast<std::uint32_t>(bits >> 32);
    const unsigned biased = (hi >> 20) & 0x7FF;
    std::uint32_t manLow = 0, manHigh = 0;
    std::uint16_t exponentWord = 0;
    if (biased != 0 || (hi & 0xFFFFF) != 0 || lo != 0) {
        std::uint32_t implicit = 0x80000000;
        std::uint16_t exponent;
        if (biased == 0) {
            exponent = 0x3C01;
            implicit = 0;
        } else if (biased == 0x7FF) {
            exponent = 0x7FFF;
        } else {
            exponent = static_cast<std::uint16_t>(biased + 0x3C00);
        }
        manHigh = lo >> 21 | (hi & 0xFFFFF) << 11 | implicit;
        manLow = lo << 11;
        while (!(manHigh & 0x80000000)) {
            manHigh = manHigh << 1 | manLow >> 31;
            manLow <<= 1;
            --exponent;
        }
        exponentWord = static_cast<std::uint16_t>((hi >> 16 & 0x8000) | exponent);
    }
    DigitString out;
    TenByteToDigits(manLow, manHigh, exponentWord, 17, false, out);
    std::memcpy(digits, out.digits, std::strlen(out.digits) + 1);
    return Decimal{out.sign, out.exponent, digits};
}

// _shift (006EF3F6): moves the string `distance` places right.
void ShiftText(char* text, int distance) {
    if (distance) std::memmove(text + distance, text, std::strlen(text) + 1);
}

// _fptostr (006F950D): `count` digits after a guard '0' (zeros past the end of the digits), rounded half
// up on the next digit; a carry into the guard moves the point, otherwise the guard is dropped.
void PlaceDigits(char* buffer, int count, Decimal& number) {
    const char* digit = number.digits;
    char* p = buffer + 1;
    buffer[0] = '0';
    int left = count;
    for (; left > 0; --left) *p++ = *digit ? *digit++ : '0';
    *p = '\0';
    if (left >= 0 && *digit > '4') {
        while (*--p == '9') *p = '0';
        ++*p;
    }
    if (buffer[0] == '1') {
        ++number.point;
    } else {
        std::memmove(buffer, buffer + 1, std::strlen(buffer + 1) + 1);
    }
}

// _cftoe2 (006EF154): d.ddde+NNN (three exponent digits) from the placed digits.
void ExponentForm(char* buffer, int decimals, bool capitals, const Decimal& number, bool general) {
    if (general) ShiftText(buffer + (number.sign == '-'), decimals > 0);
    char* p = buffer;
    if (number.sign == '-') *p++ = '-';
    if (decimals > 0) {
        p[0] = p[1];
        *++p = DecimalPoint();
    }
    p += decimals + (general ? 0 : 1);
    std::memcpy(p, "e+000", sizeof "e+000");
    if (capitals) *p = 'E';
    if (*number.digits != '0') {
        int exponent = number.point - 1;
        if (exponent < 0) {
            exponent = -exponent;
            p[1] = '-';
        }
        if (exponent >= 100) {
            p[2] = static_cast<char>(p[2] + exponent / 100);
            exponent %= 100;
        }
        if (exponent >= 10) {
            p[3] = static_cast<char>(p[3] + exponent / 10);
            exponent %= 10;
        }
        p[4] = static_cast<char>(p[4] + exponent);
    }
}

// _cftof2 (006EF26B): ddd.ddd from the placed digits.
void FixedForm(char* buffer, int decimals, const Decimal& number, bool general) {
    const int point = number.point;
    if (general && point - 1 == decimals) {
        char* end = buffer + (number.sign == '-') + point - 1;
        end[0] = '0';
        end[1] = '\0';
    }
    char* p = buffer;
    if (number.sign == '-') *p++ = '-';
    if (number.point < 1) {
        ShiftText(p, 1);
        *p++ = '0';
    } else {
        p += number.point;
    }
    if (decimals > 0) {
        ShiftText(p, 1);
        *p = DecimalPoint();
        if (number.point < 0) {
            const int zeros = general || -number.point <= decimals ? -number.point : decimals;
            ShiftText(p + 1, zeros);
            std::memset(p + 1, '0', static_cast<std::size_t>(zeros));
        }
    }
}

// _cfltcvt (006EF3A5) with _cftoe / _cftof / _cftog: %e, %f and %g of `value` into `buffer`.
void FormatFloat(double value, char* buffer, int format, int precision, bool capitals) {
    char digits[24];
    Decimal number = ToDecimal(value, digits);
    if (format == 'e' || format == 'E') {
        PlaceDigits(buffer + (precision > 0) + (number.sign == '-'), precision + 1, number);
        ExponentForm(buffer, precision, capitals, number, false);
    } else if (format == 'f') {
        PlaceDigits(buffer + (number.sign == '-'), precision + number.point, number);
        FixedForm(buffer, precision, number, false);
    } else {
        const int magnitude = number.point - 1;
        char* p = buffer + (number.sign == '-');
        PlaceDigits(p, precision, number);
        const bool rounded = magnitude < number.point - 1;
        const int placed = number.point - 1;
        if (placed < -4 || placed >= precision) {
            ExponentForm(buffer, precision, capitals, number, true);
        } else {
            if (rounded) {
                while (*p++) {
                }
                p[-2] = '\0';
            }
            FixedForm(buffer, precision, number, true);
        }
    }
}

// _forcdecpt (006EEFF5): a decimal point after the leading digits (or before an exponent).
void ForceDecimalPoint(char* text) {
    if (Tolower(*text) != 'e') {
        do {
            ++text;
        } while (CharClass(*text) & kDigit);
    }
    char hold = *text;
    *text = DecimalPoint();
    do {
        ++text;
        const char next = *text;
        *text = hold;
        hold = next;
    } while (*text != '\0');
}

// _cropzeros (006EF04F): trailing zeros of the fraction, and then a bare point, removed.
void CropZeros(char* text) {
    const char point = DecimalPoint();
    while (*text && *text != point) ++text;
    if (*text++) {
        while (*text && *text != 'e' && *text != 'E') ++text;
        const char* stop = text--;
        while (*text == '0') --text;
        if (*text == point) --text;
        while ((*++text = *stop++) != '\0') {
        }
    }
}

// wctomb (006F9846): one wide character in the locale's code page; -1 (errno EILSEQ) when it has none.
int WideToBytes(char* to, wchar_t c) {
    if (LocaleHandle() == 0) {
        if (static_cast<unsigned short>(c) < 0x100) {
            *to = static_cast<char>(c);
            return 1;
        }
    } else {
        BOOL defaultUsed = FALSE;
        const int size = gameapi::WideCharToMultiByte(LocaleCodePage(), WC_COMPOSITECHECK | WC_SEPCHARS, &c, 1, to,
                                                      MbCurMax(), nullptr, &defaultUsed);
        if (size != 0 && !defaultUsed) return size;
    }
    SetIllegalSequence();
    return -1;
}

// ---- _output (006F17D4) ----

// FL_* flags.
constexpr unsigned kSignPlus = 0x1;
constexpr unsigned kSignSpace = 0x2;
constexpr unsigned kLeft = 0x4;
constexpr unsigned kLeadZero = 0x8;
constexpr unsigned kLong = 0x10;
constexpr unsigned kShort = 0x20;
constexpr unsigned kSigned = 0x40;
constexpr unsigned kAlternate = 0x80;
constexpr unsigned kNegative = 0x100;
constexpr unsigned kForceOctal = 0x200;
constexpr unsigned kWide = 0x800;
constexpr unsigned kInt64 = 0x8000;

// __lookuptable (007476D0): for ' '..'x' the character class in the low nibble; the next state in the
// high nibble of entry class * 8 + state.
constexpr unsigned char kFormatLookup[89] = {
    0x06, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x03, 0x06, 0x00, 0x06, 0x02, 0x10,
    0x04, 0x45, 0x45, 0x45, 0x05, 0x05, 0x05, 0x05, 0x05, 0x35, 0x30, 0x00, 0x50, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x28, 0x38, 0x50, 0x58, 0x07, 0x08, 0x00, 0x37, 0x30, 0x30, 0x57, 0x50, 0x07, 0x00,
    0x00, 0x20, 0x20, 0x08, 0x00, 0x00, 0x00, 0x00, 0x08, 0x60, 0x68, 0x60, 0x60, 0x60, 0x60, 0x00,
    0x00, 0x70, 0x70, 0x78, 0x78, 0x78, 0x78, 0x08, 0x07, 0x08, 0x00, 0x00, 0x07, 0x00, 0x08, 0x08,
    0x08, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x07, 0x08,
};
enum State { kNormal, kPercent, kFlag, kWidth, kDot, kPrecision, kSize, kType };

// ANSI_STRING / UNICODE_STRING (%Z).
struct CountedString {
    short length;
    short maximum;
    void* buffer;
};

// The original's text buffer is 512 bytes; a wider number would overrun it there.
constexpr int kBufferSize = 512;

template <typename Sink>
class Output {
public:
    explicit Output(Sink& sink) : sink_(sink) {}

    int Run(const char* format, va_list args) {
        const unsigned char* f = reinterpret_cast<const unsigned char*>(format);
        int state = kNormal;
        unsigned flags = 0;
        int width = 0;
        int precision = -1;
        bool capitals = false;
        bool noOutput = false;
        bool wideText = false;
        int prefixLength = 0;
        char prefix[2] = {};
        int radix = 0;
        int hexAdd = 0;
        // Kept from one conversion to the next, as in the original.
        const char* text = nullptr;
        int textLength = 0;
        char buffer[kBufferSize];
        std::vector<char> floatBuffer;
        unsigned char c;
        while ((c = *f++) != 0 && written_ >= 0) {
            const signed char sc = static_cast<signed char>(c);
            const int cls = sc < ' ' || sc > 'x' ? 0 : kFormatLookup[sc - ' '] & 0xF;
            state = kFormatLookup[cls * 8 + state] >> 4;
            switch (state) {
            case kNormal:
            normal:
                wideText = false;
                if (CharClass(c) & kLeadByte) {
                    PutChar(sc);
                    c = *f++;
                }
                PutChar(static_cast<signed char>(c));
                break;
            case kPercent:
                precision = -1;
                capitals = false;
                noOutput = false;
                width = 0;
                prefixLength = 0;
                flags = 0;
                wideText = false;
                break;
            case kFlag:
                switch (c) {
                case ' ': flags |= kSignSpace; break;
                case '#': flags |= kAlternate; break;
                case '+': flags |= kSignPlus; break;
                case '-': flags |= kLeft; break;
                case '0': flags |= kLeadZero; break;
                }
                break;
            case kWidth:
                if (c == '*') {
                    width = va_arg(args, int);
                    if (width < 0) {
                        flags |= kLeft;
                        width = -width;
                    }
                } else {
                    width = width * 10 + (sc - '0');
                }
                break;
            case kDot:
                precision = 0;
                break;
            case kPrecision:
                if (c == '*') {
                    precision = va_arg(args, int);
                    if (precision < 0) precision = -1;
                } else {
                    precision = precision * 10 + (sc - '0');
                }
                break;
            case kSize:
                if (c == 'I') {
                    if (f[0] != '6' || f[1] != '4') {
                        state = kNormal;
                        goto normal;
                    }
                    f += 2;
                    flags |= kInt64;
                } else if (c == 'h') {
                    flags |= kShort;
                } else if (c == 'l') {
                    flags |= kLong;
                } else if (c == 'w') {
                    flags |= kWide;
                }
                break;
            case kType: {
                bool integer = false;
                switch (c) {
                case 'C':
                    if (!(flags & (kShort | kLong | kWide))) flags |= kWide;
                    [[fallthrough]];
                case 'c':
                    if (!(flags & (kLong | kWide))) {
                        buffer[0] = static_cast<char>(va_arg(args, int));
                        textLength = 1;
                    } else {
                        const wchar_t wc = static_cast<wchar_t>(va_arg(args, int));
                        textLength = WideToBytes(buffer, wc);
                        if (textLength < 0) noOutput = true;
                    }
                    text = buffer;
                    break;
                case 'Z': {
                    const CountedString* counted = va_arg(args, const CountedString*);
                    if (!counted || !counted->buffer) {
                        text = "(null)";
                        textLength = static_cast<int>(std::strlen(text));
                    } else if (flags & kWide) {
                        wideText = true;
                        textLength = static_cast<int>(static_cast<unsigned>(static_cast<int>(counted->length)) >> 1);
                        text = static_cast<const char*>(counted->buffer);
                    } else {
                        wideText = false;
                        textLength = counted->length;
                        text = static_cast<const char*>(counted->buffer);
                    }
                    break;
                }
                case 'S':
                    if (!(flags & (kShort | kLong | kWide))) flags |= kWide;
                    [[fallthrough]];
                case 's': {
                    int limit = precision == -1 ? INT_MAX : precision;
                    const void* arg = va_arg(args, const void*);
                    if (!(flags & (kLong | kWide))) {
                        const char* s = arg ? static_cast<const char*>(arg) : "(null)";
                        const char* e = s;
                        for (; limit != 0 && *e; ++e) --limit;
                        text = s;
                        textLength = static_cast<int>(e - s);
                    } else {
                        const wchar_t* s = arg ? static_cast<const wchar_t*>(arg) : L"(null)";
                        wideText = true;
                        const wchar_t* e = s;
                        for (; limit != 0 && *e; ++e) --limit;
                        text = reinterpret_cast<const char*>(s);
                        textLength = static_cast<int>(e - s);
                    }
                    break;
                }
                case 'n': {
                    void* to = va_arg(args, void*);
                    if (flags & kShort) {
                        *static_cast<short*>(to) = static_cast<short>(written_);
                    } else {
                        *static_cast<int*>(to) = written_;
                    }
                    noOutput = true;
                    break;
                }
                case 'E':
                case 'G':
                    capitals = true;
                    c = static_cast<unsigned char>(c + ('a' - 'A'));
                    [[fallthrough]];
                case 'e':
                case 'f':
                case 'g': {
                    flags |= kSigned;
                    if (precision < 0) {
                        precision = 6;
                    } else if (precision == 0 && c == 'g') {
                        precision = 1;
                    }
                    char* out = buffer;
                    if (precision > kBufferSize - 400) {
                        floatBuffer.assign(static_cast<std::size_t>(precision) + 400, '\0');
                        out = floatBuffer.data();
                    }
                    const double value = va_arg(args, double);
                    FormatFloat(value, out, c, precision, capitals);
                    if ((flags & kAlternate) && precision == 0) ForceDecimalPoint(out);
                    if (c == 'g' && !(flags & kAlternate)) CropZeros(out);
                    text = out;
                    if (*out == '-') {
                        flags |= kNegative;
                        ++text;
                    }
                    textLength = static_cast<int>(std::strlen(text));
                    break;
                }
                case 'd':
                case 'i':
                    flags |= kSigned;
                    radix = 10;
                    integer = true;
                    break;
                case 'u':
                    radix = 10;
                    integer = true;
                    break;
                case 'o':
                    radix = 8;
                    if (flags & kAlternate) flags |= kForceOctal;
                    integer = true;
                    break;
                case 'p':
                    precision = 8;
                    [[fallthrough]];
                case 'X':
                case 'x':
                    hexAdd = c == 'x' ? 'a' - '9' - 1 : 'A' - '9' - 1;
                    radix = 16;
                    if (flags & kAlternate) {
                        prefix[0] = '0';
                        prefix[1] = static_cast<char>('Q' + hexAdd);   // 'X' or 'x'
                        prefixLength = 2;
                    }
                    integer = true;
                    break;
                }
                if (integer) FormatInteger(args, flags, precision, radix, hexAdd, prefixLength, buffer, text, textLength);
                if (!noOutput) Emit(flags, width, prefix, prefixLength, text, textLength, wideText);
                break;
            }
            }
        }
        return written_;
    }

private:
    void PutChar(int c) {
        if (!sink_.Put(static_cast<char>(c))) {
            written_ = -1;
        } else {
            ++written_;
        }
    }
    void PutRepeated(char c, int count) {
        while (count-- > 0) {
            PutChar(c);
            if (written_ == -1) break;
        }
    }
    void PutText(const char* s, int count) {
        while (count-- > 0) {
            PutChar(*s++);
            if (written_ == -1) break;
        }
    }

    // The integer conversions: the argument by its size, its magnitude in `radix` written backwards
    // from the end of the buffer, at least `precision` digits.
    static void FormatInteger(va_list& args, unsigned& flags, int& precision, int radix, int hexAdd, int& prefixLength,
                              char* buffer, const char*& text, int& textLength) {
        std::uint64_t number;
        if (flags & kInt64) {
            number = va_arg(args, std::uint64_t);
        } else if (flags & kShort) {
            const int arg = va_arg(args, int);
            number = static_cast<std::uint64_t>(static_cast<std::int64_t>(
                flags & kSigned ? static_cast<int>(static_cast<short>(arg)) : arg & 0xFFFF));
        } else if (flags & kSigned) {
            number = static_cast<std::uint64_t>(static_cast<std::int64_t>(va_arg(args, int)));
        } else {
            number = va_arg(args, unsigned);
        }
        if ((flags & kSigned) && static_cast<std::int64_t>(number) < 0) {
            number = 0 - number;
            flags |= kNegative;
        }
        if (!(flags & kInt64)) number &= 0xFFFFFFFF;
        if (precision < 0) {
            precision = 1;
        } else {
            flags &= ~kLeadZero;
            if (precision > kBufferSize - 1) precision = kBufferSize - 1;
        }
        if (number == 0) prefixLength = 0;
        char* last = buffer + kBufferSize - 1;
        char* p = last;
        while (precision-- > 0 || number != 0) {
            int digit = static_cast<int>(number % static_cast<unsigned>(radix)) + '0';
            number /= static_cast<unsigned>(radix);
            if (digit > '9') digit += hexAdd;
            *p-- = static_cast<char>(digit);
        }
        textLength = static_cast<int>(last - p);
        ++p;
        if ((flags & kForceOctal) && (*p != '0' || textLength == 0)) {
            *--p = '0';
            ++textLength;
        }
        text = p;
    }

    // The sign or prefix, padding to the width (spaces before, zeros after the prefix, or spaces after
    // when left-aligned) and the text; wide text goes through wctomb character by character.
    void Emit(unsigned flags, int width, char* prefix, int prefixLength, const char* text, int textLength, bool wideText) {
        if (flags & kSigned) {
            if (flags & kNegative) {
                prefix[0] = '-';
                prefixLength = 1;
            } else if (flags & kSignPlus) {
                prefix[0] = '+';
                prefixLength = 1;
            } else if (flags & kSignSpace) {
                prefix[0] = ' ';
                prefixLength = 1;
            }
        }
        const int padding = width - prefixLength - textLength;
        if (!(flags & (kLeft | kLeadZero))) PutRepeated(' ', padding);
        PutText(prefix, prefixLength);
        if ((flags & kLeadZero) && !(flags & kLeft)) PutRepeated('0', padding);
        if (wideText && textLength > 0) {
            const wchar_t* w = reinterpret_cast<const wchar_t*>(text);
            for (int left = textLength; left != 0; --left) {
                char bytes[8];
                const int size = WideToBytes(bytes, *w++);
                if (size < 1) break;
                PutText(bytes, size);
            }
        } else {
            PutText(text, textLength);
        }
        if (flags & kLeft) PutRepeated(' ', padding);
    }

    Sink& sink_;
    int written_ = 0;
};

// sprintf's string FILE (_cnt 0x7FFFFFFF): no limit.
struct UnboundedSink {
    char* next;
    bool Put(char c) {
        *next++ = c;
        return true;
    }
};
// A string FILE with room for `left` bytes; past it every write fails (_flsbuf on _IOSTRG).
struct BoundedSink {
    char* next;
    std::size_t left;
    bool Put(char c) {
        if (left == 0) return false;
        --left;
        *next++ = c;
        return true;
    }
};
struct VectorSink {
    std::vector<char>& text;
    bool Put(char c) {
        text.push_back(c);
        return true;
    }
};

} // namespace

int __cdecl Vsprintf(char* buffer, const char* format, va_list args) {
    UnboundedSink sink{buffer};
    const int written = Output<UnboundedSink>(sink).Run(format, args);
    *sink.next = '\0';
    return written;
}
EI_REPLACE(0x006ECC47, libcrt::Vsprintf);

int __cdecl Sprintf(char* buffer, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int written = Vsprintf(buffer, format, args);
    va_end(args);
    return written;
}
EI_REPLACE(0x006E8715, libcrt::Sprintf);

int FormatInto(char* buffer, std::size_t room, const char* format, va_list args) {
    BoundedSink sink{buffer, room};
    const int written = Output<BoundedSink>(sink).Run(format, args);
    sink.Put('\0');
    return written;
}

// The text formatted in memory, then written with the game's fwrite (one write, as _stbuf's temporary
// buffer gives the original for an unbuffered stream).
int __cdecl Vfprintf(gamecrt::CrtFile* file, const char* format, va_list args) {
    std::vector<char> text;
    VectorSink sink{text};
    const int written = Output<VectorSink>(sink).Run(format, args);
    if (text.empty()) return written;
    return gamecrt::fwrite(text.data(), 1, text.size(), file) == text.size() ? written : -1;
}

int __cdecl Fprintf(gamecrt::CrtFile* file, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int written = Vfprintf(file, format, args);
    va_end(args);
    return written;
}
EI_REPLACE(0x006EAEB0, libcrt::Fprintf);

int __cdecl Printf(const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int written = Vfprintf(StandardOutput(), format, args);
    va_end(args);
    return written;
}
EI_REPLACE(0x006E955F, libcrt::Printf);

gamecrt::CrtFile* StandardOutput() { return reinterpret_cast<gamecrt::CrtFile*>(0x00798080); }   // &_iob[1]

// ---- atof: __strgtold12 and _ld12cvt ----

namespace {

// __mtold12 (006FA94C): the decimal digits (0..10 each) as a 96-bit integer, normalised so the top bit of
// the 80-bit mantissa is set, exponent 0x404E less the shifts.
void DigitsToLd12(const char* digits, int count, Ld12& x) {
    std::uint16_t exponent = 0x404E;
    x = Ld12{};
    for (; count > 0; --count, ++digits) {
        const Ld12 copy = x;
        ShiftLeft(x);
        ShiftLeft(x);
        Add(x, copy);
        ShiftLeft(x);
        Ld12 digit{};
        Set32(digit, 0, static_cast<std::uint32_t>(static_cast<int>(*digits)));
        Add(x, digit);
    }
    while (Get32(x, 4) == 0) {
        const std::uint32_t lo = Get32(x, 0), med = Get32(x, 2);
        Set32(x, 4, med >> 16);
        Set32(x, 2, lo >> 16 | med << 16);
        Set32(x, 0, lo << 16);
        exponent = static_cast<std::uint16_t>(exponent - 16);
    }
    while (!(Get32(x, 4) & 0x8000)) {
        ShiftLeft(x);
        --exponent;
    }
    x.w[5] = exponent;
}

bool IsDigitClass(unsigned char c) { return (CharClass(c) & kDigit) != 0; }
bool IsNonZeroDigit(unsigned char c) {
    const signed char s = static_cast<signed char>(c);
    return s > '0' && s <= '9';
}
bool IsExponentLetter(unsigned char c) { return c == 'D' || c == 'E' || c == 'd' || c == 'e'; }

constexpr unsigned kNoDigits = 4;    // SLD_NODIGITS
constexpr unsigned kOverflow = 2;    // SLD_OVERFLOW
constexpr unsigned kUnderflow = 1;   // SLD_UNDERFLOW
constexpr int kMaxPower = 5200;      // a larger decimal exponent overflows (or underflows)
constexpr int kKeptDigits = 25;      // digits stored; the 25th only feeds the (buggy) rounding

// __strgtold12 (006F9989) as atof calls it (no scale, no implicit exponent, 10-byte multiply flag off):
// the number at `text` (after spaces, tabs and line breaks) as a 12-byte long double. At most 24
// significant digits: when there are more the 24th is incremented if it is 5 or more (the original
// tests and bumps the last kept digit, not the first dropped one). Decimal exponents beyond 5200 give
// infinity or zero.
unsigned StringToLd12(const char* text, Ld12& result) {
    char digits[32];
    char* next = digits;
    int count = 0;
    std::uint16_t sign = 0;
    int exponentSign = 1;
    bool foundDigit = false;
    int power = 0;
    int adjust = 0;
    const char point = DecimalPoint();
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    int state = 0;
    bool done = false;
    // After the mantissa's digits: a sign starts an exponent only with implicit E (not for atof), an
    // exponent letter does.
    auto afterMantissa = [&](unsigned char c) {
        if (c == '+' || c == '-') {
            done = true;
        } else if (IsExponentLetter(c)) {
            state = 6;
        } else {
            done = true;
        }
    };
    while (!done) {
        unsigned char c = *p++;
        switch (state) {
        case 0:   // start
            if (IsNonZeroDigit(c)) {
                state = 3;
                --p;
            } else if (c == static_cast<unsigned char>(point)) {
                state = 5;
            } else if (c == '+') {
                sign = 0;
                state = 2;
            } else if (c == '-') {
                sign = 0x8000;
                state = 2;
            } else if (c == '0') {
                state = 1;
            } else {
                done = true;
            }
            break;
        case 1:   // leading zeros
            foundDigit = true;
            if (IsNonZeroDigit(c)) {
                state = 3;
                --p;
            } else if (c == static_cast<unsigned char>(point)) {
                state = 4;
            } else if (c == '+' || c == '-') {
                done = true;
            } else if (c == '0') {
            } else if (IsExponentLetter(c)) {
                state = 6;
            } else {
                done = true;
            }
            break;
        case 2:   // after the sign
            if (IsNonZeroDigit(c)) {
                state = 3;
                --p;
            } else if (c == static_cast<unsigned char>(point)) {
                state = 5;
            } else if (c == '0') {
                state = 1;
            } else {
                done = true;
            }
            break;
        case 3:   // integer digits
            foundDigit = true;
            while (IsDigitClass(c)) {
                if (count < kKeptDigits) {
                    ++count;
                    *next++ = static_cast<char>(c - '0');
                } else {
                    ++adjust;
                }
                c = *p++;
            }
            if (c == static_cast<unsigned char>(point)) {
                state = 4;
            } else {
                afterMantissa(c);
            }
            break;
        case 4:   // fraction digits
            foundDigit = true;
            if (count == 0) {
                while (c == '0') {
                    --adjust;
                    c = *p++;
                }
            }
            while (IsDigitClass(c)) {
                if (count < kKeptDigits) {
                    ++count;
                    --adjust;
                    *next++ = static_cast<char>(c - '0');
                }
                c = *p++;
            }
            afterMantissa(c);
            break;
        case 5:   // a leading decimal point
            if (IsDigitClass(c)) {
                state = 4;
                --p;
            } else {
                done = true;
            }
            break;
        case 6:   // after the exponent letter
            if (IsNonZeroDigit(c)) {
                state = 9;
                --p;
            } else if (c == '+') {
                state = 7;
            } else if (c == '-') {
                exponentSign = -1;
                state = 7;
            } else if (c == '0') {
                state = 8;
            } else {
                done = true;
            }
            break;
        case 7:   // after the exponent's sign
            if (IsNonZeroDigit(c)) {
                state = 9;
                --p;
            } else if (c == '0') {
                state = 8;
            } else {
                done = true;
            }
            break;
        case 8:   // the exponent's leading zeros
            while (c == '0') c = *p++;
            if (IsNonZeroDigit(c)) {
                state = 9;
                --p;
            } else {
                done = true;
            }
            break;
        case 9:   // exponent digits
            power = 0;
            while (IsDigitClass(c)) {
                power = power * 10 + (static_cast<signed char>(c) - '0');
                if (power > kMaxPower) {
                    power = kMaxPower + 1;
                    break;
                }
                c = *p++;
            }
            while (IsDigitClass(c)) c = *p++;
            done = true;
            break;
        }
    }
    result = Ld12{};
    if (!foundDigit) {
        result.w[5] = sign;
        return kNoDigits;
    }
    unsigned flags = 0;
    if (count > 24) {
        if (digits[23] > 4) ++digits[23];
        count = 24;
        ++adjust;
        --next;
    }
    if (count > 0) {
        while (*--next == 0) {
            --count;
            ++adjust;
        }
        DigitsToLd12(digits, count, result);
        int exponent = exponentSign < 0 ? -power : power;
        exponent += adjust;
        if (exponent > kMaxPower) {
            PutInfinity(result, 0);
            flags = kOverflow;
        } else if (exponent < -kMaxPower) {
            PutZero(result);
            flags = kUnderflow;
        } else {
            MultiplyByPowerOfTen(result, exponent, false);
        }
    }
    result.w[5] |= sign;
    return flags;
}

// _ld12cvt's mantissa helpers on three dwords, most significant first (006F9079 .. 006F91E6).
using Mantissa = std::uint32_t[3];

// _ZeroTail: the bits after `bit` (not `bit` itself) are zero.
bool ZeroTail(const Mantissa man, int bit) {
    const int word = bit / 32;
    const int shift = 31 - bit % 32;
    if (man[word] & ~(~0u << shift)) return false;
    for (int i = word + 1; i < 3; ++i) {
        if (man[i]) return false;
    }
    return true;
}

// _IncMan: one unit at `bit`; the carry out of the top.
bool IncrementAt(Mantissa man, int bit) {
    int word = bit / 32;
    std::uint32_t before = man[word];
    man[word] += 1u << (31 - bit % 32);
    bool carry = man[word] < before;
    for (--word; word >= 0 && carry; --word) {
        before = man[word];
        man[word] += 1;
        carry = man[word] < before;
    }
    return carry;
}

// _RoundMan: keep `precision` bits; up when the next bit is set and some bit after the one following
// it is too (the original's tail test skips that bit, so a tie and some above-tie values round down).
// The rounding bit itself stays. Returns the carry out of the top.
bool RoundTo(Mantissa man, int precision) {
    const int word = precision / 32;
    const int shift = 31 - precision % 32;
    bool carry = false;
    if ((man[word] & 1u << shift) && !ZeroTail(man, precision + 1)) carry = IncrementAt(man, precision - 1);
    man[word] &= ~0u << shift;
    for (int i = word + 1; i < 3; ++i) man[i] = 0;
    return carry;
}

// _ShrMan.
void ShiftRightBy(Mantissa man, int bits) {
    const int words = bits / 32;
    const int shift = bits % 32;
    std::uint32_t carry = 0;
    for (int i = 0; i < 3; ++i) {
        const std::uint32_t word = man[i];
        man[i] = word >> shift | carry;
        carry = shift ? (word & ~(~0u << shift)) << (32 - shift) : 0;
    }
    for (int i = 2; i >= 0; --i) man[i] = i < words ? 0 : man[i - words];
}

// _ld12tod (006F93DF, _ld12cvt with the double format at 0079ADE0): rounded to 53 bits (RoundTo), the
// denormal range rounded again from the saved mantissa, overflow to infinity, a denormal 12-byte value
// to zero.
double Ld12ToDouble(const Ld12& x) {
    constexpr int kMaxExponent = 1024;
    constexpr int kMinExponent = -1023;
    constexpr int kPrecision = 53;
    constexpr int kExponentWidth = 11;
    constexpr int kBias = 1023;
    const std::uint16_t word = x.w[5];
    int exponent = (word & 0x7FFF) - 0x3FFF;
    std::uint32_t man[3] = {Get32(x, 3), Get32(x, 1), static_cast<std::uint32_t>(x.w[0]) << 16};
    std::uint32_t biased = 0;
    if (exponent == -0x3FFF) {
        man[0] = man[1] = man[2] = 0;
    } else {
        const std::uint32_t saved[3] = {man[0], man[1], man[2]};
        if (RoundTo(man, kPrecision)) ++exponent;
        if (exponent < kMinExponent - kPrecision) {
            man[0] = man[1] = man[2] = 0;
        } else if (exponent <= kMinExponent) {
            man[0] = saved[0];
            man[1] = saved[1];
            man[2] = saved[2];
            ShiftRightBy(man, kMinExponent - exponent);
            RoundTo(man, kPrecision);
            ShiftRightBy(man, kExponentWidth + 1);
        } else if (exponent >= kMaxExponent) {
            man[0] = 0x80000000;
            man[1] = man[2] = 0;
            ShiftRightBy(man, kExponentWidth);
            biased = kMaxExponent + kBias;
        } else {
            biased = static_cast<std::uint32_t>(exponent + kBias);
            man[0] &= 0x7FFFFFFF;
            ShiftRightBy(man, kExponentWidth);
        }
    }
    const std::uint32_t high = man[0] | biased << (31 - kExponentWidth) | (word & 0x8000 ? 0x80000000u : 0);
    const std::uint64_t bits = static_cast<std::uint64_t>(high) << 32 | man[1];
    double value;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

} // namespace

// White space by the character classes, then _fltin2's conversion: 0 when there are no digits.
double __cdecl Atof(const char* text) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(text);
    while (CharClass(*p) & kSpace) ++p;
    Ld12 value;
    if (StringToLd12(reinterpret_cast<const char*>(p), value) & kNoDigits) return 0.0;
    return Ld12ToDouble(value);
}
EI_REPLACE(0x006E911A, libcrt::Atof);

} // namespace libcrt
#endif
