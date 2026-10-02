#include "mfc.h"

#include "ei/hook.h"

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace mfc {

// _afxPchNil: the shared empty-string data pointer.
CString::CString() : data_(*reinterpret_cast<char**>(0x00796E48)) {}

__declspec(naked) CString::CString(const char*) EI_ORIGINAL(0x007009DE)
__declspec(naked) CString::CString(const CString&) EI_ORIGINAL(0x007006E5)
__declspec(naked) CString::~CString() EI_ORIGINAL(0x00700970)
__declspec(naked) const CString& CString::operator=(const char*) EI_ORIGINAL(0x00700AF9)
__declspec(naked) const CString& CString::operator=(const CString&) EI_ORIGINAL(0x00700AA9)

__declspec(naked) CString __stdcall operator+(const CString&, const char*) EI_ORIGINAL(0x00700C05)
__declspec(naked) CString __stdcall operator+(const char*, const CString&) EI_ORIGINAL(0x00700C79)
__declspec(naked) CString __stdcall operator+(const CString&, const CString&) EI_ORIGINAL(0x00700B9F)

} // namespace mfc
