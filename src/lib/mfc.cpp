#include "mfc.h"
#include "mfc_try.h"
#include "mfc_archive.h"
#include "mfc_coll.h"
#include "mfc_exception.h"
#include "mfc_file.h"
#include "mfc_string.h"
#include "mfc_time.h"

#include "ei/hook.h"
#include "game/game_data.h"

#include <cstring>
#include <new>

namespace mfc {

namespace {
// The classes here share their layouts with the implementations (mfc_string.h, mfc_file.h,
// mfc_time.h, mfc_coll.h, mfc_archive.h), which are what the registered addresses run.
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
CString::CString() : data_(impl::PchNil()) {}

CString::CString(const char* s) { ::new (static_cast<void*>(this)) impl::CString(s); }
CString::CString(const char* s, int length) { ::new (static_cast<void*>(this)) impl::CString(s, length); }
CString::CString(const CString& s) { ::new (static_cast<void*>(this)) impl::CString(*Impl(&s)); }
CString::~CString() { Impl(this)->~CString(); }
const CString& CString::operator=(const char* s) { *Impl(this) = s; return *this; }
const CString& CString::operator=(const CString& s) { *Impl(this) = *Impl(&s); return *this; }
#endif

// (the implementation's Format is FormatV over its arguments).
void __cdecl CString::Format(const char* format, ...) {
    va_list arguments;
    va_start(arguments, format);
    Impl(this)->FormatV(format, arguments);
    va_end(arguments);
}
void CString::FormatV(const char* format, va_list arguments) { Impl(this)->FormatV(format, arguments); }
#ifndef EI_VECTORS_REPLAY
CString CString::Left(int count) const {
    CString result(kUnset);
    Place(result, Impl(this)->Left(count));
    return result;
}
CString CString::Right(int count) const {
    CString result(kUnset);
    Place(result, Impl(this)->Right(count));
    return result;
}
CString CString::Mid(int first) const {
    CString result(kUnset);
    Place(result, Impl(this)->Mid(first));
    return result;
}
CString CString::Mid(int first, int count) const {
    CString result(kUnset);
    Place(result, Impl(this)->Mid(first, count));
    return result;
}
#endif
int CString::Remove(char ch) { return Impl(this)->Remove(ch); }
int CString::FindOneOf(const char* set) const { return Impl(this)->FindOneOf(set); }
int CString::ReverseFind(char ch) const { return Impl(this)->ReverseFind(ch); }
#ifndef EI_VECTORS_REPLAY
int CString::Find(const char* sub) const { return Impl(this)->Find(sub); }
int CString::Find(const char* sub, int start) const { return Impl(this)->Find(sub, start); }
void CString::Empty() { Impl(this)->Empty(); }
#endif
char* CString::GetBufferSetLength(int length) { return Impl(this)->GetBufferSetLength(length); }
char* CString::GetBuffer(int minLength) { return Impl(this)->GetBuffer(minLength); }
void CString::ReleaseBuffer(int newLength) { Impl(this)->ReleaseBuffer(newLength); }
void CString::MakeLower() { Impl(this)->MakeLower(); }
void CString::AssignChar(char c) { *Impl(this) = c; }
void CString::TrimRight(const char* targets) { Impl(this)->TrimRight(targets); }
void CString::TrimRight() { Impl(this)->TrimRight(); }
void CString::TrimRight(char target) { Impl(this)->TrimRight(target); }
void CString::TrimLeft() { Impl(this)->TrimLeft(); }
void CString::TrimLeft(char target) { Impl(this)->TrimLeft(target); }
const CString& CString::operator+=(char c) { *Impl(this) += c; return *this; }
int CString::Find(char c) const { return Impl(this)->Find(c); }
int CString::Find(char c, int start) const { return Impl(this)->Find(c, start); }
#ifndef EI_VECTORS_REPLAY
const CString& CString::operator+=(const CString& s) { *Impl(this) += *Impl(&s); return *this; }
const CString& CString::operator+=(const char* s) { *Impl(this) += s; return *this; }
#endif

namespace {
impl::CFile* Impl(CFile* file) { return reinterpret_cast<impl::CFile*>(file); }
impl::CFileFind* Impl(CFileFind* find) { return reinterpret_cast<impl::CFileFind*>(find); }
const impl::CFileFind* Impl(const CFileFind* find) { return reinterpret_cast<const impl::CFileFind*>(find); }
impl::CStringArray* Impl(CStringArray* array) { return reinterpret_cast<impl::CStringArray*>(array); }
const impl::CStringArray* Impl(const CStringArray* array) { return reinterpret_cast<const impl::CStringArray*>(array); }
} // namespace

CFile::CFile() { Impl(this)->Construct(); }
CFile::~CFile() { Impl(this)->Destruct(); }
BOOL CFile::Open(const char* path, unsigned flags, void* error) {
    return Impl(this)->Open(path, flags, static_cast<impl::CFileException*>(error));
}
unsigned CFile::GetLength() { return Impl(this)->GetLength(); }
unsigned CFile::Read(void* buffer, unsigned count) { return Impl(this)->Read(buffer, count); }
void CFile::Write(const void* buffer, unsigned count) { Impl(this)->Write(buffer, count); }
void CFile::Close() { Impl(this)->Close(); }

#ifndef EI_VECTORS_REPLAY   // ei_vectors does not link mfc_time.cpp
CTime* CTime::Construct(int year, int month, int day, int hour, int minute, int second, int dst) {
    reinterpret_cast<impl::CTime*>(this)->Construct(year, month, day, hour, minute, second, dst);
    return this;
}
CString CTime::Format(const char* format) const {
    CString result(CString::kUnset);
    Place(result, reinterpret_cast<const impl::CTime*>(this)->Format(format));
    return result;
}
#endif

CFileFind::CFileFind() { Impl(this)->Construct(); }
CFileFind::~CFileFind() { Impl(this)->Destruct(); }
BOOL CFileFind::FindFile(const char* pattern, unsigned long flags) { return Impl(this)->FindFile(pattern, flags); }
CString CFileFind::GetFilePath() const {
    CString result(CString::kUnset);
    Place(result, Impl(this)->GetFilePath());
    return result;
}
unsigned long CFileFind::GetLength() const { return Impl(this)->GetLength(); }
CString CFileFind::GetFileName() const {
    CString result(CString::kUnset);
    Place(result, Impl(this)->GetFileName());
    return result;
}
CString CFileFind::GetTitle() const {
    CString result(CString::kUnset);
    Place(result, Impl(this)->GetFileTitle());
    return result;
}
BOOL CFileFind::FindNext() { return Impl(this)->FindNextFile(); }
BOOL CFileFind::IsDots() const { return Impl(this)->IsDots(); }
void CFileFind::Close() { Impl(this)->Close(); }

void CStringArray::Construct() { Impl(this)->Construct(); }
void CStringArray::Destruct() { Impl(this)->Destruct(); }
void CStringArray::SetSize(int newSize, int newGrowBy) { Impl(this)->SetSize(newSize, newGrowBy); }
void CStringArray::SetAtGrow(int index, const CString& value) { Impl(this)->SetAtGrow(index, *Impl(&value)); }
void CStringArray::SetAtGrow(int index, const char* text) { Impl(this)->SetAtGrow(index, text); }
void CStringArray::RemoveAt(int index, int count) { Impl(this)->RemoveAt(index, count); }
void CStringArray::Copy(const CStringArray& source) { Impl(this)->Copy(*Impl(&source)); }
int CStringArray::Append(const CStringArray& source) { return Impl(this)->Append(*Impl(&source)); }
void CStringArray::InsertAt(int index, const CString& value, int count) {
    Impl(this)->InsertAt(index, *Impl(&value), count);
}

namespace {
impl::CArchive* Impl(CArchive* archive) { return reinterpret_cast<impl::CArchive*>(archive); }
} // namespace

unsigned CArchive::Read(void* buffer, unsigned bytes) { return Impl(this)->Read(buffer, bytes); }
void CArchive::Write(const void* buffer, unsigned bytes) { Impl(this)->Write(buffer, bytes); }
unsigned long CArchive::ReadCount() { return Impl(this)->ReadCount(); }
void CArchive::WriteCount(unsigned long count) { Impl(this)->WriteCount(count); }
void CArchive::Flush() { Impl(this)->Flush(); }

#ifndef EI_VECTORS_REPLAY
CString __stdcall operator+(const CString& a, const char* b) {
    CString result(CString::kUnset);
    Place(result, *Impl(&a) + b);
    return result;
}
CString __stdcall operator+(const char* a, const CString& b) {
    CString result(CString::kUnset);
    Place(result, a + *Impl(&b));
    return result;
}
CString __stdcall operator+(const CString& a, const CString& b) {
    CString result(CString::kUnset);
    Place(result, *Impl(&a) + *Impl(&b));
    return result;
}
CString __stdcall operator+(const CString& a, char b) {
    CString result(CString::kUnset);
    Place(result, *Impl(&a) + b);
    return result;
}
CString __stdcall operator+(char a, const CString& b) {
    CString result(CString::kUnset);
    Place(result, a + *Impl(&b));
    return result;
}
#endif

// AFX_EXCEPTION_LINK (mfc_try.h): the constructor, and AfxTryCleanup for the
// destructor.
ExceptionLink::ExceptionLink() { ConstructExceptionLink(this); }
ExceptionLink::~ExceptionLink() { TryCleanup(); }

} // namespace mfc
