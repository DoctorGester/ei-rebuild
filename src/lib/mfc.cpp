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
} // namespace

// ei_vectors (EI_VECTORS_REPLAY) has its own CString and CPlex core (tests/vectors/replay_mfc.cpp).
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

namespace {
impl::CFile* Impl(CFile* file) { return reinterpret_cast<impl::CFile*>(file); }
impl::CFileFind* Impl(CFileFind* find) { return reinterpret_cast<impl::CFileFind*>(find); }
const impl::CFileFind* Impl(const CFileFind* find) { return reinterpret_cast<const impl::CFileFind*>(find); }
} // namespace

CFile::CFile() { Impl(this)->Construct(); }
CFile::CFile(const char* path, unsigned flags) { Impl(this)->ConstructOpen(path, flags); }
CFile::~CFile() { Impl(this)->Destruct(); }
BOOL CFile::Open(std::string_view path, unsigned flags, void* error) {
    return Impl(this)->Open(std::string(path).c_str(), flags, static_cast<impl::CFileException*>(error));
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
std::string CTime::Format(const char* format) const {
    return std::string(reinterpret_cast<const impl::CTime*>(this)->Format(format));
}
#endif

CFileFind::CFileFind() { Impl(this)->Construct(); }
CFileFind::~CFileFind() { Impl(this)->Destruct(); }
BOOL CFileFind::FindFile(std::string_view pattern, unsigned long flags) {
    return Impl(this)->FindFile(std::string(pattern).c_str(), flags);
}
std::string CFileFind::GetFilePath() const { return std::string(Impl(this)->GetFilePath()); }
unsigned long CFileFind::GetLength() const { return Impl(this)->GetLength(); }
std::string CFileFind::GetFileName() const { return std::string(Impl(this)->GetFileName()); }
std::string CFileFind::GetTitle() const { return std::string(Impl(this)->GetFileTitle()); }
BOOL CFileFind::FindNext() { return Impl(this)->FindNextFile(); }
BOOL CFileFind::MatchesMask(unsigned long mask) const { return Impl(this)->MatchesMask(mask); }
BOOL CFileFind::IsDots() const { return Impl(this)->IsDots(); }
void CFileFind::Close() { Impl(this)->Close(); }

namespace {
impl::CArchive* Impl(CArchive* archive) { return reinterpret_cast<impl::CArchive*>(archive); }
} // namespace

void CArchive::Flush() { Impl(this)->Flush(); }

// AFX_EXCEPTION_LINK (mfc_try.h): the constructor, and AfxTryCleanup for the destructor.
ExceptionLink::ExceptionLink() { ConstructExceptionLink(this); }
ExceptionLink::~ExceptionLink() { TryCleanup(); }

} // namespace mfc
