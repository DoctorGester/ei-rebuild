#pragma once

// Payloads of the game CRT's formatted text in the "crt" family (vectors.h): sprintf / vsprintf and atof
// (src/lib/crt_format.cpp, S4a). Recorded by record_crt_format.cpp, replayed by replay_crt_format.cpp.
//
// "@CrtFormatLocale" (fixture): what the conversions read of the CRT's locale, the decimal point
// (00797FE8) and the class words of _pctype (00797DD8) for 0..255. Taken at the first call after the
// game has set its LC_CTYPE locale; a call while the locale differs from it is not recorded.
//
// "vsprintf" (sprintf's calls too: both are _output on an unbounded string FILE): the format, the
// arguments _output reads in order (ArgumentKind and its value; a %s text up to the precision), the
// count and the text written. Calls with %n, %Z or wide text (%C, %S, %lc, %ls, %wc, %ws) are not
// recorded, nor calls whose output overlaps the format or a text argument. Bucket 1: a float
// conversion (%e %f %g %E %G).
//
// "atof": the text and the result's bits. Bucket 1: an exponent letter, more than 17 characters or a
// byte >= 0x80.

#include "vectors.h"

#include <cstddef>
#include <cstdint>

namespace ei::vectors::crt_format {

constexpr std::uintptr_t kDecimalPoint = 0x00797FE8;   // __decimal_point[0]
constexpr std::uintptr_t kCharClasses = 0x00797DD8;    // _pctype

// Longest format, text argument or atof text, and output kept.
constexpr std::size_t kMaxText = 0x400;
constexpr std::size_t kMaxOutput = 0x1000;

enum ArgumentKind : std::uint8_t {
    kInt32,        // 4 bytes
    kInt64,        // 8 bytes (%I64)
    kDouble,       // 8 bytes, the bits
    kText,         // a string (Writer::PutString)
    kNullText,     // a null %s pointer
};

} // namespace ei::vectors::crt_format
