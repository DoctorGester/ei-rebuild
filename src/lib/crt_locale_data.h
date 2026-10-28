#pragma once

// The locale and multibyte state the class, case and conversion functions read (crt.cpp, crt_format.cpp,
// mfc_resource.cpp): the host CRT's (setlocale and _setmbcp are the host's; the text manager's Create sets
// LC_CTYPE from the "Font CharSet" option, then the multibyte code page from it, text_sources.cpp). In a file of
// its own so ei_vectors links it with crt_format.cpp (its replay takes the decimal point and the class words from
// the "@CrtFormatLocale" fixture, tests/vectors/crt_format_vectors.h, through SetReplayLocale).

#include <cstdint>

namespace libcrt {

// _pctype: the class words of LC_CTYPE's locale, byte 0's first (a negative char indexes the words before).
const std::uint16_t* CharClasses();
// LC_CTYPE's locale as a Win32 locale id (VC6's __lc_handle[LC_CTYPE]): 0 for "C".
unsigned CtypeLocale();
// LC_CTYPE's ANSI code page (__lc_codepage).
unsigned CtypeCodePage();
// __mb_cur_max: the bytes of the longest character (1, 2 for a double-byte code page).
int MbCurMax();
// The LC_NUMERIC locale's decimal point ('.'; the game never sets LC_NUMERIC).
char DecimalPoint();
// The multibyte code page's flags (_mbctype + 1: a byte's flags at its value) and _mbcasemap (each byte's
// other case).
const std::uint8_t* MultibyteFlags();

#ifdef EI_VECTORS_REPLAY
// ei_vectors: the decimal point and the class words recorded in the game (`classes` stays the caller's).
void SetReplayLocale(char decimalPoint, const std::uint16_t* classes);
#endif

} // namespace libcrt
