#pragma once

// The game CRT's locale and multibyte state (crt_locale_data.cpp): what setlocale / _setmbcp / __initmbctable
// (crt_locale.cpp) write and the class, case and conversion functions (crt.cpp, crt_format.cpp, mfc_resource.cpp)
// read. In a file of its own so ei_vectors links it with crt_format.cpp (its replay starts from the "C" locale and
// takes the decimal point from the "@CrtFormatLocale" fixture, tests/vectors/crt_format_vectors.h).

#include "crt.h"

#include <cstdint>

namespace libcrt {

// __lc_handle: each category's language id (0 for "C"); LC_CTYPE's is the one the class functions test.
extern unsigned g_lcHandle[6];
// __lc_codepage (LC_CTYPE's ANSI code page) and __lc_collate_cp.
extern unsigned g_lcCodePage;
extern unsigned g_lcCollateCodePage;
// _ctype: the "C" locale's class words, EOF's (0) first. _pctype / _pwctype point one entry on, at byte 0's word,
// or at the heap tables setlocale's LC_CTYPE builds (crt_locale.cpp's InitCtype).
extern const std::uint16_t kCtypeC[257];
extern const std::uint16_t* g_pctype;
extern const std::uint16_t* g_pwctype;
// The heap blocks behind _pctype / _pwctype (null for "C").
extern std::uint16_t* g_ctypeBlock;
extern std::uint16_t* g_wideCtypeBlock;
// __lc_id: each category's LC_ID.
extern LocaleId g_lcId[6];
// __mb_cur_max: the bytes of the longest character (1, 2 for a double-byte code page).
extern int g_mbCurMax;
// __decimal_point: the LC_NUMERIC locale's (".").
extern char g_decimalPoint[2];

// getqloc's cache: the last name asked for and its qualified name ("C" at first), its LC_ID and code page.
extern char g_cacheIn[132];
extern char g_cacheOut[132];
extern LocaleId g_cachedId;
extern unsigned g_cachedCodePage;

// The multibyte code page (mbctype.c): __mbcodepage, __ismbcodepage, __mblcid, __mbulinfo, _mbctype (257 bytes, a
// byte's flags at + 1), _mbcasemap (each byte's other case), fSystemSet (the code page came from the system) and
// __initmbctable's flag.
extern unsigned g_mbCodePage;
extern int g_isMbCodePage;
extern unsigned g_mbLcid;
extern unsigned g_mbUlInfo[3];
extern std::uint8_t g_mbCtype[257];
extern std::uint8_t g_mbCaseMap[256];
extern int g_mbSystemSet;
extern int g_mbTableReady;

} // namespace libcrt
