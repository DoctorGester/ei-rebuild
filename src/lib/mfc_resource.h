#pragma once

// MFC 4.2's resource strings and their formatting (strcore.cpp, winstr.cpp, appui1.cpp) as the
// game's copy has them (ticket S8b, docs/notes/standalone.md section 2). The strings are game.exe's
// string table (MFC's 0xF0xx / 0xF1xx prompts): LoadStringA on the module state's resource handle,
// which is null in both builds (the game never runs AfxWinInit), through the exe's import slot. In
// game.exe that is the process's own module; in the standalone image the host's LoadStringA wrapper
// maps null to game.exe loaded as a data file (S14). Registered at the library addresses
// (mfc_resource.cpp), checked in game by verify_mfc_resource.cpp until S21d.

#include "mfc_string.h"

namespace mfc::impl {

// MFC's resource ids the library code loads (afxres.h).
constexpr unsigned kIdsUnnamedFile = 0xF006;         // AFX_IDS_UNNAMED_FILE
constexpr unsigned kIdpNoErrorAvailable = 0xF020;    // AFX_IDP_NO_ERROR_AVAILABLE
constexpr unsigned kIdpFileNone = 0xF1A0;            // AFX_IDP_FILE_NONE (+ a CFileException cause)
constexpr unsigned kIdpArchiveNone = 0xF1B0;         // AFX_IDP_ARCH_NONE (+ a CArchiveException cause)

// AfxLoadString (00702B92): string `id` into `buffer` (`size` bytes); its length, 0 with an empty
// buffer when there is none.
int __stdcall AfxLoadString(unsigned id, char* buffer, unsigned size);

// AfxFormatStrings (0070B1AD): `format` into `out` with "%1".."%9" and "%A".."%Z" replaced by
// strings[0] ... (a null string by nothing, an index at or past `count` by '?'; "%0" reads the
// pointer before the array, as the original does). A lead byte copies its trail byte with it.
void __stdcall AfxFormatStrings(CString& out, const char* format, const char* const* strings, int count);
// AfxFormatStrings(CString&, UINT, ...) (0070B173): the same with string `formatId` (at most 255
// bytes) as the format; `out` is left as it is when the string does not load.
void __stdcall AfxFormatStringsId(CString& out, unsigned formatId, const char* const* strings, int count);
// AfxFormatString1 (0070B2AC): the format string `formatId` with one string.
void __stdcall AfxFormatString1(CString& out, unsigned formatId, const char* string);

} // namespace mfc::impl
