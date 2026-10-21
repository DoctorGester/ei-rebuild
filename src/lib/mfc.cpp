#include "mfc.h"
#include "mfc_try.h"
#include "mfc_archive.h"
#include "mfc_coll.h"
#include "mfc_exception.h"
#include "mfc_file.h"
#include "mfc_string.h"
#include "mfc_time.h"

#include "ei/hook.h"

#include <cstring>
#include <new>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace mfc {

namespace {
// The classes here share their layouts with the implementations (mfc_string.h, mfc_file.h,
// mfc_time.h, mfc_coll.h, mfc_archive.h), which are what the original addresses run.
impl::CString* Impl(CString* s) { return reinterpret_cast<impl::CString*>(s); }
const impl::CString* Impl(const CString* s) { return reinterpret_cast<const impl::CString*>(s); }
// `result` (unset storage) takes the implementation string `s`'s data: no copy, no reference
// counting; `s` is left empty (its destructor then does nothing).
void Place(CString& result, impl::CString&& s) {
    std::memcpy(static_cast<void*>(&result), &s, sizeof(result));
    s.Init();
}
} // namespace

// ei_vectors (EI_VECTORS_REPLAY) has its own CString and CPlex core (tests/vectors/replay_mfc.cpp): the
// functions in the #ifndef blocks below. The other members must not be called there unless their
// implementation links into it (mfc_string.cpp, mfc_archive.cpp).
#ifndef EI_VECTORS_REPLAY
// _afxPchNil: the shared empty-string data pointer.
CString::CString() : data_(*reinterpret_cast<char**>(0x00796E48)) {}

CString::CString(const char* s) { ::new (static_cast<void*>(this)) impl::CString(s); }                       // 007009DE
CString::CString(const char* s, int length) { ::new (static_cast<void*>(this)) impl::CString(s, length); }   // 006FD4DE
CString::CString(const CString& s) { ::new (static_cast<void*>(this)) impl::CString(*Impl(&s)); }            // 007006E5
CString::~CString() { Impl(this)->~CString(); }                                                              // 00700970
const CString& CString::operator=(const char* s) { *Impl(this) = s; return *this; }                          // 00700AF9
const CString& CString::operator=(const CString& s) { *Impl(this) = *Impl(&s); return *this; }               // 00700AA9
#endif

// 006FDF5F (the implementation's Format is FormatV over its arguments).
void __cdecl CString::Format(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    Impl(this)->FormatV(format, arguments);
    va_end(arguments);
}
void CString::FormatV(const char* format, va_list arguments) { Impl(this)->FormatV(format, arguments); }   // 006FDC12
#ifndef EI_VECTORS_REPLAY
CString CString::Left(int count) const {     // 006FDAE9
    CString result(kUnset);
    Place(result, Impl(this)->Left(count));
    return result;
}
CString CString::Right(int count) const {   // 006FDA6D
    CString result(kUnset);
    Place(result, Impl(this)->Right(count));
    return result;
}
CString CString::Mid(int first) const {       // 006FD9B4
    CString result(kUnset);
    Place(result, Impl(this)->Mid(first));
    return result;
}
CString CString::Mid(int first, int count) const {                                                                    // 006FD9D7
    CString result(kUnset);
    Place(result, Impl(this)->Mid(first, count));
    return result;
}
#endif
int CString::Remove(char ch) { return Impl(this)->Remove(ch); }                         // 006FD963
int CString::FindOneOf(const char* set) const { return Impl(this)->FindOneOf(set); }    // 00700ECC
int CString::ReverseFind(char ch) const { return Impl(this)->ReverseFind(ch); }        // 006FDBB7
#ifndef EI_VECTORS_REPLAY
int CString::Find(const char* sub) const { return Impl(this)->Find(sub); }                     // 006FDBD9
int CString::Find(const char* sub, int start) const { return Impl(this)->Find(sub, start); }   // 006FDBE7
void CString::Empty() { Impl(this)->Empty(); }                                                 // 007008FB
#endif
char* CString::GetBufferSetLength(int length) { return Impl(this)->GetBufferSetLength(length); }   // 00700E17
char* CString::GetBuffer(int minLength) { return Impl(this)->GetBuffer(minLength); }               // 00700DA0
void CString::ReleaseBuffer(int newLength) { Impl(this)->ReleaseBuffer(newLength); }               // 00700DEF
void CString::MakeLower() { Impl(this)->MakeLower(); }                                             // 00700EFE
void CString::AssignChar(char c) { *Impl(this) = c; }                                              // 006FD559
void CString::TrimRight(const char* targets) { Impl(this)->TrimRight(targets); }                   // 006FE08A
void CString::TrimRight() { Impl(this)->TrimRight(); }                                             // 006FE11F
void CString::TrimRight(char target) { Impl(this)->TrimRight(target); }                            // 006FE0DD
void CString::TrimLeft() { Impl(this)->TrimLeft(); }                                               // 006FE216
void CString::TrimLeft(char target) { Impl(this)->TrimLeft(target); }                              // 006FE1D3
const CString& CString::operator+=(char c) { *Impl(this) += c; return *this; }                     // 00700D73
int CString::Find(char c) const { return Impl(this)->Find(c); }                                    // 00700E91
int CString::Find(char c, int start) const { return Impl(this)->Find(c, start); }                   // 00700E9F
#ifndef EI_VECTORS_REPLAY
const CString& CString::operator+=(const CString& s) { *Impl(this) += *Impl(&s); return *this; }   // 00700D88
const CString& CString::operator+=(const char* s) { *Impl(this) += s; return *this; }              // 00700D4C
#endif

namespace {
impl::CFile* Impl(CFile* file) { return reinterpret_cast<impl::CFile*>(file); }
impl::CFileFind* Impl(CFileFind* find) { return reinterpret_cast<impl::CFileFind*>(find); }
const impl::CFileFind* Impl(const CFileFind* find) { return reinterpret_cast<const impl::CFileFind*>(find); }
impl::CStringArray* Impl(CStringArray* array) { return reinterpret_cast<impl::CStringArray*>(array); }
const impl::CStringArray* Impl(const CStringArray* array) { return reinterpret_cast<const impl::CStringArray*>(array); }
} // namespace

CFile::CFile() { Impl(this)->Construct(); }                                         // 00701036
CFile::~CFile() { Impl(this)->Destruct(); }                                         // 0070112B
BOOL CFile::Open(const char* path, unsigned flags, void* error) {                   // 007011E8
    return Impl(this)->Open(path, flags, static_cast<impl::CFileException*>(error));
}
unsigned CFile::GetLength() { return Impl(this)->GetLength(); }                     // 007014E2
unsigned CFile::Read(void* buffer, unsigned count) { return Impl(this)->Read(buffer, count); }   // 00701305
void CFile::Write(const void* buffer, unsigned count) { Impl(this)->Write(buffer, count); }      // 0070133F
void CFile::Close() { Impl(this)->Close(); }                                        // 00701403

#ifndef EI_VECTORS_REPLAY   // ei_vectors does not link mfc_time.cpp
CTime* CTime::Construct(int year, int month, int day, int hour, int minute, int second, int dst) {   // 006FE3BB
    reinterpret_cast<impl::CTime*>(this)->Construct(year, month, day, hour, minute, second, dst);
    return this;
}
CString CTime::Format(const char* format) const {                                   // 006FE795
    CString result(CString::kUnset);
    Place(result, reinterpret_cast<const impl::CTime*>(this)->Format(format));
    return result;
}
#endif

CFileFind::CFileFind() { Impl(this)->Construct(); }                                 // 006E6370
CFileFind::~CFileFind() { Impl(this)->Destruct(); }                                 // 006E63AD
BOOL CFileFind::FindFile(const char* pattern, unsigned long flags) { return Impl(this)->FindFile(pattern, flags); }   // 006E6433
CString CFileFind::GetFilePath() const {                                            // 006E6722
    CString result(CString::kUnset);
    Place(result, Impl(this)->GetFilePath());
    return result;
}
unsigned long CFileFind::GetLength() const { return Impl(this)->GetLength(); }      // 006E68AA
CString CFileFind::GetFileName() const {                                            // 006E684B
    CString result(CString::kUnset);
    Place(result, Impl(this)->GetFileName());
    return result;
}
CString CFileFind::GetTitle() const {                                               // 006E67BA
    CString result(CString::kUnset);
    Place(result, Impl(this)->GetFileTitle());
    return result;
}
BOOL CFileFind::FindNext() { return Impl(this)->FindNextFile(); }                   // 006E6653
BOOL CFileFind::IsDots() const { return Impl(this)->IsDots(); }                     // 006E661A
void CFileFind::Close() { Impl(this)->Close(); }                                    // 006E63E9

void CStringArray::Construct() { Impl(this)->Construct(); }                         // 006FE925
void CStringArray::Destruct() { Impl(this)->Destruct(); }                           // 006FE958
void CStringArray::SetSize(int newSize, int newGrowBy) { Impl(this)->SetSize(newSize, newGrowBy); }              // 006FE9B8
void CStringArray::SetAtGrow(int index, const CString& value) { Impl(this)->SetAtGrow(index, *Impl(&value)); }   // 006FEBFA
void CStringArray::SetAtGrow(int index, const char* text) { Impl(this)->SetAtGrow(index, text); }                // 006FEBCE
void CStringArray::RemoveAt(int index, int count) { Impl(this)->RemoveAt(index, count); }                        // 006FED3C
void CStringArray::Copy(const CStringArray& source) { Impl(this)->Copy(*Impl(&source)); }                       // 006FEB61
int CStringArray::Append(const CStringArray& source) { return Impl(this)->Append(*Impl(&source)); }             // 006FEB01
void CStringArray::InsertAt(int index, const CString& value, int count) {                                        // 006FED03
    Impl(this)->InsertAt(index, *Impl(&value), count);
}

namespace {
impl::CArchive* Impl(CArchive* archive) { return reinterpret_cast<impl::CArchive*>(archive); }
} // namespace

unsigned CArchive::Read(void* buffer, unsigned bytes) { return Impl(this)->Read(buffer, bytes); }   // 00708832
void CArchive::Write(const void* buffer, unsigned bytes) { Impl(this)->Write(buffer, bytes); }      // 00708941
unsigned long CArchive::ReadCount() { return Impl(this)->ReadCount(); }                           // 00708B3D
void CArchive::WriteCount(unsigned long count) { Impl(this)->WriteCount(count); }                 // 00708B0F
void CArchive::Flush() { Impl(this)->Flush(); }                                                   // 007089D1

#ifndef EI_VECTORS_REPLAY
CString __stdcall operator+(const CString& a, const char* b) {         // 00700C05
    CString result(CString::kUnset);
    Place(result, *Impl(&a) + b);
    return result;
}
CString __stdcall operator+(const char* a, const CString& b) {         // 00700C79
    CString result(CString::kUnset);
    Place(result, a + *Impl(&b));
    return result;
}
CString __stdcall operator+(const CString& a, const CString& b) {      // 00700B9F
    CString result(CString::kUnset);
    Place(result, *Impl(&a) + *Impl(&b));
    return result;
}
CString __stdcall operator+(const CString& a, char b) {                // 006FD56E
    CString result(CString::kUnset);
    Place(result, *Impl(&a) + b);
    return result;
}
CString __stdcall operator+(char a, const CString& b) {                // 006FD5D0
    CString result(CString::kUnset);
    Place(result, a + *Impl(&b));
    return result;
}
#endif

// AFX_EXCEPTION_LINK (mfc_try.h): the constructor (007020F1), and AfxTryCleanup (0070212F) for the
// destructor.
ExceptionLink::ExceptionLink() { ConstructExceptionLink(this); }
ExceptionLink::~ExceptionLink() { TryCleanup(); }

} // namespace mfc
