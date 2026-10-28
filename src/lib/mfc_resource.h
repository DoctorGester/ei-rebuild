#pragma once

// MFC 4.2's resource strings and their formatting (strcore.cpp, winstr.cpp, appui1.cpp) as the
// game's copy has them (docs/notes/standalone.md section 2). The strings are a table in code
// (mfc_resource.cpp) holding the texts of MFC's 0xF0xx / 0xF1xx ids our code can reach (the host has no
// string table). Defined in mfc_resource.cpp.

#include <string>

namespace mfc::impl {

// MFC's resource ids the library code loads (afxres.h).
constexpr unsigned kIdpNoErrorAvailable = 0xF020;    // AFX_IDP_NO_ERROR_AVAILABLE
constexpr unsigned kIdsResourceException = 0xF022;   // AFX_IDS_RESOURCE_EXCEPTION
constexpr unsigned kIdsMemoryException = 0xF023;     // AFX_IDS_MEMORY_EXCEPTION

// AfxLoadString: string `id` into `buffer` (`size` bytes); its length, 0 with an empty
// buffer when there is none.
int AfxLoadString(unsigned id, char* buffer, unsigned size);

// CString::LoadString: string `id`, empty when there is none.
std::string LoadResourceString(unsigned id);

} // namespace mfc::impl
