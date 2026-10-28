#pragma once

// Upper and lower case of UTF-8 text (docs/notes/standalone.md section 4).
// The original changes case through the C runtime's locale (LC_CTYPE from the "Font CharSet" option, "english":
// code page 1252) and the multibyte table of the system's ANSI code page, one byte at a time. On UTF-8 that
// corrupts text (tolower of the lead byte 0xD0 is 0xF0) and still misses Cyrillic. The CRT's case functions
// (crt.cpp: _stricmp, _strnicmp, _memicmp, _mbsicmp, _mbslwr and so CString::MakeLower, tolower / toupper, the
// character classes) and CharUpperA / CharLowerA (system_text.h) use this table instead, whatever the locale:
//   - ASCII A-Z / a-z;
//   - Latin-1 U+00C0-U+00DE / U+00E0-U+00FE (not U+00D7 / U+00F7) and the other letters of code page 1252
//     (Œ œ, Š š, Ž ž, Ÿ ÿ);
//   - Cyrillic U+0400-U+042F / U+0430-U+045F and Ґ ґ (every letter of code page 1251, the assets').
// Each pair is two UTF-8 bytes on both sides, so a case change never changes a text's length in bytes.
// Everything else (other scripts, bytes that are not UTF-8) stays as it is.

#include <cstddef>

namespace text {

// Code point `c` in lower / upper case by the table above (`c` itself when it has no pair).
unsigned LowerCodePoint(unsigned c);
unsigned UpperCodePoint(unsigned c);

// `text` lower / upper cased in place by the table, up to its terminator or `size` bytes.
void LowerInPlace(char* text, std::size_t size = static_cast<std::size_t>(-1));
void UpperInPlace(char* text, std::size_t size = static_cast<std::size_t>(-1));

// The texts compared as their lower-cased bytes (the table), up to `n` bytes: -1, 0 or 1, as the CRT's
// compares answer. `terminated`: a zero byte in either text ends the compare (_stricmp, _strnicmp);
// otherwise exactly `n` bytes are compared (_memicmp).
int CompareIgnoringCase(const char* a, const char* b, std::size_t n, bool terminated);

// Whether code point `c` is printable: not a C0 or C1 control, not DEL, not a surrogate, at most U+10FFFF.
bool IsPrintableCodePoint(unsigned c);
// The UTF-8 sequence at `text` (at most `available` bytes): its length (1 for a byte that does not start a
// valid sequence: then `*codePoint` is 0xFFFFFFFF) and its code point.
int DecodeUtf8(const char* text, std::size_t available, unsigned* codePoint);

} // namespace text
