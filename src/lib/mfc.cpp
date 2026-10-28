#include "mfc.h"
#include "mfc_try.h"
#include "mfc_archive.h"
#include "mfc_coll.h"
#include "mfc_exception.h"
#include "mfc_file.h"
#include "mfc_time.h"

#include "ei/hook.h"
#include "game/game_data.h"

#include <cstring>
#include <memory>
#include <new>

namespace mfc {

namespace {
impl::CFile* Impl(CFile* file) { return reinterpret_cast<impl::CFile*>(file); }
impl::CFileFind* Impl(CFileFind* find) { return reinterpret_cast<impl::CFileFind*>(find); }
const impl::CFileFind* Impl(const CFileFind* find) { return reinterpret_cast<const impl::CFileFind*>(find); }
} // namespace

static_assert(sizeof(CFile) == sizeof(impl::CFile) && alignof(CFile) == alignof(impl::CFile));
static_assert(sizeof(CFileFind) == sizeof(impl::CFileFind) && alignof(CFileFind) == alignof(impl::CFileFind));

CFile::CFile() { std::construct_at(Impl(this)); }
CFile::CFile(const char* path, unsigned flags) { std::construct_at(Impl(this), path, flags); }
CFile::~CFile() { std::destroy_at(Impl(this)); }
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
std::string CTime::Format(const char* format) const { return reinterpret_cast<const impl::CTime*>(this)->Format(format); }
#endif

CFileFind::CFileFind() { std::construct_at(Impl(this)); }
CFileFind::~CFileFind() { std::destroy_at(Impl(this)); }
BOOL CFileFind::FindFile(std::string_view pattern, unsigned long flags) {
    return Impl(this)->FindFile(std::string(pattern).c_str(), flags);
}
std::string CFileFind::GetFilePath() const { return Impl(this)->GetFilePath(); }
unsigned long CFileFind::GetLength() const { return Impl(this)->GetLength(); }
std::string CFileFind::GetFileName() const { return Impl(this)->GetFileName(); }
std::string CFileFind::GetTitle() const { return Impl(this)->GetFileTitle(); }
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
