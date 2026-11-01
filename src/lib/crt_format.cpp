// The game CRT's printf (str::Format, CString::Format's text): the format read as MSVC 6's _output reads it, the
// integer, character and string conversions the host's snprintf, the float ones the CRT's own: $I10_OUTPUT's 17
// digits from a 12-byte long double (96-bit mantissa products, the exe's power-of-ten tables) and _fptostr's
// rounding of that digit string, all integer arithmetic. The host's floats differ (two-digit exponents, "inf", ties
// to even), so these stay.
#include "crt.h"

#include "str.h"
#include "text_case.h"

#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace libcrt {

namespace {

// The decimal point: the C locale's (nothing sets LC_NUMERIC).
constexpr char kDecimalPoint = '.';

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
bool IsZero(const Ld12& x) { return (Get32(x, 4) & 0x7FFFFFFF) == 0 && Get32(x, 2) == 0 && Get32(x, 0) == 0; }

// __ld12mul: x *= y. Only the partial products of the upper 96 bits are summed, a carry out
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

// _pow10pos / _pow10neg: 10^1..10^7, 10^8..10^56 by 8, 10^64..10^448 by 64,
// 10^512..10^3584 by 512, 10^4096 (and their reciprocals).
constexpr Ld12 kPow10Positive[29] = /*0079AE60*/ {
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
constexpr Ld12 kPow10Negative[29] = /*0079AFC0*/ {
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

// __multtenpow12: x *= 10^power, three bits of the power per table row. A table entry whose
// extra word is at or above half is used one unit lower in its low mantissa dword ("unrounded").
void MultiplyByPowerOfTen(Ld12& x, int power) {
    if (power == 0) return;
    const Ld12* table = kPow10Positive;
    if (power < 0) {
        power = -power;
        table = kPow10Negative;
    }
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

// FOS, $I10_OUTPUT's result: the decimal exponent, the sign (' ' or '-') and the digits.
struct DigitString {
    std::int16_t exponent;
    char sign;
    char digits[24];
};

void PutZeroDigits(DigitString& out) {
    out.exponent = 0;
    out.sign = ' ';
    out.digits[0] = '0';
    out.digits[1] = '\0';
}

// $I10_OUTPUT on an 80-bit value (the low and high mantissa dwords, sign and exponent) for 17 digits:
// scale into [0.1, 1) by a power of ten estimated low from the exponent and the mantissa's top byte,
// generate 18 digits by multiplying the 96-bit fraction by ten, and round on the last one; trailing zeros
// are dropped. Infinity and NaN are written as "1#INF", "1#IND", "1#QNAN", "1#SNAN".
void TenByteToDigits(std::uint32_t low, std::uint32_t high, std::uint16_t exponentWord, DigitString& out) {
    constexpr int kDigits = 17;
    const Ld12 oneTenth = {{0xCCCC, 0xCCCC, 0xCCCC, 0xCCCC, 0xCCCC, 0x3FFB}};
    const std::uint16_t sign = exponentWord & 0x8000;
    const std::uint16_t exponent = exponentWord & 0x7FFF;
    out.sign = sign ? '-' : ' ';
    if (exponent == 0 && high == 0 && low == 0) {
        PutZeroDigits(out);
        return;
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
        return;
    }
    Ld12 value{};
    Set32(value, 1, low);
    Set32(value, 3, high);
    value.w[5] = exponent;
    const std::uint32_t estimate = ((exponent >> 8) + (high >> 24) * 2) * 0x4Du + exponent * 0x4D10u + 0xECBCED0Cu;
    std::int16_t decimal = static_cast<std::int16_t>(static_cast<std::int32_t>(estimate) >> 16);
    MultiplyByPowerOfTen(value, -decimal);
    if (value.w[5] >= 0x3FFF) {
        ++decimal;
        Multiply(value, oneTenth);
    }
    out.exponent = decimal;
    const int shift = value.w[5] - 0x3FFE;
    value.w[5] = 0;
    for (int i = 0; i < 8; ++i) ShiftLeft(value);
    if (shift < 0) {
        for (unsigned n = static_cast<unsigned>(-shift) & 0xFF; n != 0; --n) ShiftRight(value);
    }
    char* p = out.digits;
    for (int n = kDigits + 1; n > 0; --n) {
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
            return;
        }
    }
    q[1] = '\0';
}

// STRFLT: the sign, the decimal exponent (digits before the point), the digits.
struct Decimal {
    int sign;
    int point;
    char* digits;
};

// _fltout2: __dtold to 80 bits (a zero of either sign becomes +0; a denormal is normalised), then 17 digits.
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
    TenByteToDigits(manLow, manHigh, exponentWord, out);
    std::memcpy(digits, out.digits, std::strlen(out.digits) + 1);
    return Decimal{out.sign, out.exponent, digits};
}

void ShiftText(char* text, int distance) {
    if (distance) std::memmove(text + distance, text, std::strlen(text) + 1);
}

// _fptostr: `count` digits after a guard '0' (zeros past the end of the digits), rounded half
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

// _cftoe2: d.ddde+NNN (three exponent digits) from the placed digits.
void ExponentForm(char* buffer, int decimals, bool capitals, const Decimal& number, bool general) {
    if (general) ShiftText(buffer + (number.sign == '-'), decimals > 0);
    char* p = buffer;
    if (number.sign == '-') *p++ = '-';
    if (decimals > 0) {
        p[0] = p[1];
        *++p = kDecimalPoint;
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

// _cftof2: ddd.ddd from the placed digits.
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
        *p = kDecimalPoint;
        if (number.point < 0) {
            const int zeros = general || -number.point <= decimals ? -number.point : decimals;
            ShiftText(p + 1, zeros);
            std::memset(p + 1, '0', static_cast<std::size_t>(zeros));
        }
    }
}

// _cfltcvt with _cftoe / _cftof / _cftog: %e, %f and %g of `value` into `buffer`.
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

// _forcdecpt: a decimal point after the leading digits (or before an exponent).
void ForceDecimalPoint(char* text) {
    if (text::ToLower(*text) != 'e') {
        do {
            ++text;
        } while (text::IsDigit(*text));
    }
    char hold = *text;
    *text = kDecimalPoint;
    do {
        ++text;
        const char next = *text;
        *text = hold;
        hold = next;
    } while (*text != '\0');
}

// _cropzeros: trailing zeros of the fraction, and then a bare point, removed.
void CropZeros(char* text) {
    while (*text && *text != kDecimalPoint) ++text;
    if (*text++) {
        while (*text && *text != 'e' && *text != 'E') ++text;
        const char* stop = text--;
        while (*text == '0') --text;
        if (*text == kDecimalPoint) --text;
        while ((*++text = *stop++) != '\0') {
        }
    }
}

// ---- The format: MSVC 6's _output ----

constexpr unsigned kSignPlus = 0x1;
constexpr unsigned kSignSpace = 0x2;
constexpr unsigned kLeft = 0x4;
constexpr unsigned kLeadZero = 0x8;
constexpr unsigned kAlternate = 0x10;
constexpr unsigned kShort = 0x20;
constexpr unsigned kLong = 0x40;
constexpr unsigned kWide = 0x80;
constexpr unsigned kInt64 = 0x100;

// __lookuptable: for ' '..'x' the character class in the low nibble; the next state in the
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

// The text buffer is 512 bytes: an integer's precision stops at 511 digits, a float's buffer grows past
// precision 112.
constexpr int kBufferSize = 512;

// The host's snprintf of `spec` with one argument, appended.
template <typename T>
void AppendHost(std::string& out, const char* spec, T value) {
    char text[kBufferSize];
    const int length = std::snprintf(text, sizeof text, spec, value);
    // A wide character with no byte in the C locale: nothing (VC6 wrote nothing for a %lc either, but the padding
    // and the characters before it for a %ls).
    if (length < 0) return;
    if (length < kBufferSize) {
        out.append(text, static_cast<std::size_t>(length));
        return;
    }
    const std::size_t at = out.size();
    out.resize(at + static_cast<std::size_t>(length) + 1);
    std::snprintf(out.data() + at, static_cast<std::size_t>(length) + 1, spec, value);
    out.resize(at + static_cast<std::size_t>(length));
}

// The integer, character and string conversions: the host's, with VC6's sizes ('l' is 32 bits, "I64" 64; %C and %S
// are wide unless 'h'; %Z is wide with 'w' only) and %p as eight upper-case hex digits.
void HostConversion(std::string& out, char type, unsigned flags, int width, int precision, va_list& args) {
    char spec[48];
    char* p = spec;
    *p++ = '%';
    if (flags & kLeft) *p++ = '-';
    if (flags & kSignPlus) *p++ = '+';
    if (flags & kSignSpace) *p++ = ' ';
    if (flags & kAlternate) *p++ = '#';
    if (flags & kLeadZero) *p++ = '0';
    if (width > 0) p = std::to_chars(p, spec + 24, width).ptr;
    const bool integer = std::strchr("diouxXp", type) != nullptr;
    if (type == 'p') {
        type = 'X';
        precision = 8;
    }
    if (integer && precision > kBufferSize - 1) precision = kBufferSize - 1;
    if (precision >= 0) {
        *p++ = '.';
        p = std::to_chars(p, spec + 40, precision).ptr;
    }
    if (integer) {
        if (flags & kInt64) {
            std::memcpy(p, "I64", 3);
            p += 3;
        } else if (flags & kShort) {
            *p++ = 'h';
        }
    } else if (type == 'Z') {
        *p++ = flags & kWide ? 'w' : 'h';   // the host's bare %Z is wide
    } else {
        const bool upper = type == 'C' || type == 'S';
        if ((flags & (kLong | kWide)) || (upper && !(flags & kShort))) *p++ = 'l';
        if (upper) type = static_cast<char>(type + ('a' - 'A'));
    }
    *p++ = type;
    *p = '\0';
    if (integer && (flags & kInt64)) {
        AppendHost(out, spec, va_arg(args, std::uint64_t));
    } else if (integer || type == 'c') {
        AppendHost(out, spec, va_arg(args, int));
    } else {
        AppendHost(out, spec, va_arg(args, const void*));
    }
}

// %e %E %f %g %G: VC6's digits (FormatFloat), then the sign and the padding as _output writes them (spaces before,
// zeros after the sign, or spaces after when left-aligned).
void FloatConversion(std::string& out, char type, unsigned flags, int width, int precision, double value) {
    bool capitals = false;
    if (type == 'E' || type == 'G') {
        capitals = true;
        type = static_cast<char>(type + ('a' - 'A'));
    }
    if (precision < 0) {
        precision = 6;
    } else if (precision == 0 && type == 'g') {
        precision = 1;
    }
    char buffer[kBufferSize];
    std::vector<char> large;
    char* text = buffer;
    if (precision > kBufferSize - 400) {
        large.assign(static_cast<std::size_t>(precision) + 400, '\0');
        text = large.data();
    }
    FormatFloat(value, text, type, precision, capitals);
    if ((flags & kAlternate) && precision == 0) ForceDecimalPoint(text);
    if (type == 'g' && !(flags & kAlternate)) CropZeros(text);
    char sign = '\0';
    if (*text == '-') {
        sign = '-';
        ++text;
    } else if (flags & kSignPlus) {
        sign = '+';
    } else if (flags & kSignSpace) {
        sign = ' ';
    }
    const int length = static_cast<int>(std::strlen(text));
    const int padding = width - (sign ? 1 : 0) - length;
    if (!(flags & (kLeft | kLeadZero)) && padding > 0) out.append(static_cast<std::size_t>(padding), ' ');
    if (sign) out += sign;
    if ((flags & kLeadZero) && !(flags & kLeft) && padding > 0) out.append(static_cast<std::size_t>(padding), '0');
    out.append(text, static_cast<std::size_t>(length));
    if ((flags & kLeft) && padding > 0) out.append(static_cast<std::size_t>(padding), ' ');
}

} // namespace

std::string Format(const char* format, va_list arguments) {
    va_list args;   // a copy HostConversion can take by reference (a va_list parameter is a pointer on x86-64 POSIX)
    va_copy(args, arguments);
    std::string out;
    int state = kNormal;
    unsigned flags = 0;
    int width = 0;
    int precision = -1;
    for (const char* f = format; *f != '\0';) {
        const char c = *f++;
        const int cls = c < ' ' || c > 'x' ? 0 : kFormatLookup[c - ' '] & 0xF;
        state = kFormatLookup[cls * 8 + state] >> 4;
        switch (state) {
        case kNormal:
            out += c;
            break;
        case kPercent:
            flags = 0;
            width = 0;
            precision = -1;
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
                width = width * 10 + (c - '0');
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
                precision = precision * 10 + (c - '0');
            }
            break;
        case kSize:
            if (c == 'I') {
                if (f[0] != '6' || f[1] != '4') {   // not "I64": the 'I' is text
                    state = kNormal;
                    out += c;
                    break;
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
        case kType:
            if (std::strchr("eEfgG", c)) {
                FloatConversion(out, c, flags, width, precision, va_arg(args, double));
            } else if (c == 'n') {
                void* to = va_arg(args, void*);
                if (flags & kShort) {
                    *static_cast<short*>(to) = static_cast<short>(out.size());
                } else {
                    *static_cast<int*>(to) = static_cast<int>(out.size());
                }
            } else if (c != 'B') {   // 'B' converts nothing (VC6 wrote the previous conversion's text again)
                HostConversion(out, c, flags, width, precision, args);
            }
            break;
        }
    }
    va_end(args);
    return out;
}

} // namespace libcrt

// CString::FormatV's text (str.h): up to the first zero byte, as ReleaseBuffer(-1) left it.
std::string str::FormatV(const char* format, va_list arguments) {
    std::string text = libcrt::Format(format, arguments);
    text.resize(std::strlen(text.c_str()));
    return text;
}
