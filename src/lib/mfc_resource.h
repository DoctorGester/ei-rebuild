#pragma once

// MFC 4.2's resource strings and their formatting (strcore.cpp, winstr.cpp, appui1.cpp) as the
// game's copy has them (docs/notes/standalone.md section 2). The strings are a table in code
// (mfc_resource.cpp) holding the texts of MFC's 0xF0xx / 0xF1xx ids our code can reach (the host has no
// string table). Defined in mfc_resource.cpp.

#include <string>

namespace mfc::impl {

// MFC's resource ids the library code loads (afxres.h).
constexpr unsigned kIdsUnnamedFile = 0xF006;         // AFX_IDS_UNNAMED_FILE
constexpr unsigned kIdpNoErrorAvailable = 0xF020;    // AFX_IDP_NO_ERROR_AVAILABLE
constexpr unsigned kIdsNotSupportedException = 0xF021;   // AFX_IDS_NOT_SUPPORTED_EXCEPTION
constexpr unsigned kIdsResourceException = 0xF022;   // AFX_IDS_RESOURCE_EXCEPTION
constexpr unsigned kIdsMemoryException = 0xF023;     // AFX_IDS_MEMORY_EXCEPTION
constexpr unsigned kIdpFileNone = 0xF1A0;            // AFX_IDP_FILE_NONE (+ a CFileException cause)
constexpr unsigned kIdpArchiveNone = 0xF1B0;         // AFX_IDP_ARCH_NONE (+ a CArchiveException cause)

// AfxLoadString: string `id` into `buffer` (`size` bytes); its length, 0 with an empty
// buffer when there is none.
int AfxLoadString(unsigned id, char* buffer, unsigned size);

// CString::LoadString: string `id`, empty when there is none.
std::string LoadResourceString(unsigned id);

// AfxFormatString1: string `formatId` (at most 255 bytes) with "%1" replaced by `string` (null: by nothing), any
// other "%2".."%9" / "%A".."%Z" by '?' ("%0" reads the pointer before the array: never in the table's texts). A lead
// byte copies its trail byte with it. Empty when the format does not load.
std::string AfxFormatString1(unsigned formatId, const char* string);

} // namespace mfc::impl
