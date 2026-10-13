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

__declspec(naked) void __cdecl CString::Format(const char*, ...) EI_ORIGINAL(0x006FDF5F)
__declspec(naked) CString CString::Left(int) const EI_ORIGINAL(0x006FDAE9)
__declspec(naked) CString CString::Right(int) const EI_ORIGINAL(0x006FDA6D)
__declspec(naked) CString CString::Mid(int) const EI_ORIGINAL(0x006FD9B4)
__declspec(naked) int CString::FindOneOf(const char*) const EI_ORIGINAL(0x00700ECC)
__declspec(naked) int CString::ReverseFind(char) const EI_ORIGINAL(0x006FDBB7)
__declspec(naked) int CString::Find(const char*) const EI_ORIGINAL(0x006FDBD9)
__declspec(naked) int CString::Find(const char*, int) const EI_ORIGINAL(0x006FDBE7)
__declspec(naked) void CString::Empty() EI_ORIGINAL(0x007008FB)
__declspec(naked) char* CString::GetBufferSetLength(int) EI_ORIGINAL(0x00700E17)
__declspec(naked) char* CString::GetBuffer(int) EI_ORIGINAL(0x00700DA0)
__declspec(naked) void CString::ReleaseBuffer(int) EI_ORIGINAL(0x00700DEF)
__declspec(naked) void CString::MakeLower() EI_ORIGINAL(0x00700EFE)
__declspec(naked) void CString::TrimRight(const char*) EI_ORIGINAL(0x006FE08A)
__declspec(naked) const CString& CString::operator+=(const CString&) EI_ORIGINAL(0x00700D88)
__declspec(naked) const CString& CString::operator+=(const char*) EI_ORIGINAL(0x00700D4C)

__declspec(naked) CFile::CFile() EI_ORIGINAL(0x00701036)
__declspec(naked) CFile::~CFile() EI_ORIGINAL(0x0070112B)
__declspec(naked) BOOL CFile::Open(const char*, unsigned, void*) EI_ORIGINAL(0x007011E8)
__declspec(naked) unsigned CFile::GetLength() EI_ORIGINAL(0x007014E2)
__declspec(naked) unsigned CFile::Read(void*, unsigned) EI_ORIGINAL(0x00701305)
__declspec(naked) void CFile::Close() EI_ORIGINAL(0x00701403)

__declspec(naked) CFileFind::CFileFind() EI_ORIGINAL(0x006E6370)
__declspec(naked) CFileFind::~CFileFind() EI_ORIGINAL(0x006E63AD)
__declspec(naked) BOOL CFileFind::FindFile(const char*, unsigned long) EI_ORIGINAL(0x006E6433)
__declspec(naked) unsigned long CFileFind::GetLength() const EI_ORIGINAL(0x006E68AA)

__declspec(naked) void CStringArray::Construct() EI_ORIGINAL(0x006FE925)
__declspec(naked) void CStringArray::Destruct() EI_ORIGINAL(0x006FE958)
__declspec(naked) void CStringArray::SetSize(int, int) EI_ORIGINAL(0x006FE9B8)
__declspec(naked) void CStringArray::SetAtGrow(int, const CString&) EI_ORIGINAL(0x006FEBFA)
__declspec(naked) void CStringArray::SetAtGrow(int, const char*) EI_ORIGINAL(0x006FEBCE)
__declspec(naked) void CStringArray::RemoveAt(int, int) EI_ORIGINAL(0x006FED3C)
__declspec(naked) void CStringArray::Copy(const CStringArray&) EI_ORIGINAL(0x006FEB61)

__declspec(naked) unsigned CArchive::Read(void*, unsigned) EI_ORIGINAL(0x00708832)
__declspec(naked) void CArchive::Write(const void*, unsigned) EI_ORIGINAL(0x00708941)
__declspec(naked) unsigned long CArchive::ReadCount() EI_ORIGINAL(0x00708B3D)
__declspec(naked) void CArchive::WriteCount(unsigned long) EI_ORIGINAL(0x00708B0F)
__declspec(naked) void CArchive::Flush() EI_ORIGINAL(0x007089D1)

__declspec(naked) CPlex* __stdcall PlexCreate(CPlex*&, unsigned, unsigned) EI_ORIGINAL(0x006FE8EF)
__declspec(naked) void __fastcall PlexFreeDataChain(CPlex*) EI_ORIGINAL(0x006FE90F)

__declspec(naked) CString __stdcall operator+(const CString&, const char*) EI_ORIGINAL(0x00700C05)
__declspec(naked) CString __stdcall operator+(const char*, const CString&) EI_ORIGINAL(0x00700C79)
__declspec(naked) CString __stdcall operator+(const CString&, const CString&) EI_ORIGINAL(0x00700B9F)

} // namespace mfc
