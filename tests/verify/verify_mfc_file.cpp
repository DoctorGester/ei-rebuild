// Checks for MFC's CFile, CStdioFile, CFileFind and CTime (src/lib/mfc_file.cpp, mfc_time.cpp,
// ticket S8). The functions that only read run as ours and as the original and the results are
// compared (a new object or string built by the original into scratch storage after ours, SV2h1).
// CFile::Read and CStdioFile's char* reader read twice: the original, then back to where it started
// and ours; the CString line reader reads ours first. CFile::Open runs the original on a twin object
// whose handle is closed (and strings freed) before ours opens. The rest
// (the constructors that start with a call, writes, seeks, closes, the searches) run ours only, with
// checks of the state they leave. A start-up check drives the classes through their addresses in a
// private temporary folder (written, read back, searched, removed).
#include "verify.h"

#include "../../src/lib/gameapi.h"
#include "../../src/lib/mfc_file.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#pragma warning(disable : 4740) // naked forwarders

namespace {

using ei::verify::Check;
using mfc::impl::CFile;
using mfc::impl::CFileException;
using mfc::impl::CFileFind;
using mfc::impl::CStdioFile;
using mfc::impl::CString;
using mfc::impl::CTime;
void* s_unused;

const char* Text(const CString& s) { return static_cast<const char*>(s); }
bool Same(const CString& a, const CString& b) { return std::strcmp(Text(a), Text(b)) == 0; }

// Storage for an object the original constructs.
template <class T>
struct Slot {
    alignas(T) unsigned char raw[sizeof(T)];
    T* Get() { return reinterpret_cast<T*>(raw); }
};

DWORD FilePosition(HANDLE file) { return gameapi::SetFilePointer(file, 0, nullptr, FILE_CURRENT); }

// ---- Paths -----------------------------------------------------------------------------------------

// 00701870: MOV EAX,imm32 (5 bytes).
BOOL(__stdcall* s_fullPath)(char*, const char*);
BOOL __stdcall FullPathVerify(char* out, const char* path) {
    char expected[MAX_PATH];
    const BOOL expectedResult = s_fullPath(expected, path);
    const BOOL result = mfc::impl::FullPath(out, path);
    Check("AfxFullPath", true, result == expectedResult && std::strcmp(out, expected) == 0, "\"%.80s\": \"%.80s\" %d (original \"%.80s\" %d)",
          path, out, result, expected, expectedResult);
    return result;
}

// 00701940: MOV ECX,[ESP+8]; PUSH EBX (5 bytes).
void(__stdcall* s_getRoot)(const char*, CString&);
void __stdcall GetRootVerify(const char* path, CString& root) {
    mfc::impl::GetRoot(path, root);
    CString expected;
    s_getRoot(path, expected);
    Check("AfxGetRoot", true, Same(expected, root), "\"%.80s\": \"%.80s\" (original \"%.80s\")", path, Text(root), Text(expected));
}

// GetRuntimeClass: MOV EAX,imm32 (5 bytes).
template <int Id>
struct RuntimeClass {
    static inline void*(__fastcall* original)(const void*);
    static inline void*(__fastcall* ours)(const void*);
    static inline const char* name;
    static void* __fastcall Verify(const void* self) {
        void* expected = original(self);
        void* actual = ours(self);
        Check(name, expected, actual, "");
        return actual;
    }
};
using FileClass = RuntimeClass<0>;
using StdioFileClass = RuntimeClass<1>;
using FileFindClass = RuntimeClass<2>;

// ---- CFile -----------------------------------------------------------------------------------------

bool SameFile(const CFile& a, const CFile& b) {
    return a.vtable == b.vtable && a.handle == b.handle && a.closeOnDelete == b.closeOnDelete && Same(a.fileName, b.fileName);
}

// 00701036 / 0070106C: MOV EAX,ECX; MOV ECX,[afxEmptyString] (8 bytes).
CFile*(__fastcall* s_construct)(CFile*);
CFile* __fastcall ConstructVerify(CFile* self) {
    Slot<CFile> twin;
    s_construct(twin.Get());
    CFile* result = self->Construct();
    Check("CFile::CFile()", true, result == self && SameFile(*twin.Get(), *self), "");
    twin.Get()->fileName.~CString();
    return result;
}

CFile*(__fastcall* s_constructHandle)(CFile*, void*, HANDLE);
CFile* __fastcall ConstructHandleVerify(CFile* self, void*, HANDLE file) {
    Slot<CFile> twin;
    s_constructHandle(twin.Get(), nullptr, file);
    CFile* result = self->ConstructHandle(file);
    Check("CFile::CFile(int)", true, result == self && SameFile(*twin.Get(), *self), "handle %p", file);
    twin.Get()->fileName.~CString();
    return result;
}

CFile* __fastcall ConstructOpenVerify(CFile* self, void*, const char* path, unsigned flags) {
    CFile* result = self->ConstructOpen(path, flags);   // throws when the file does not open
    Check("CFile::CFile(path, flags)", true, result == self && self->handle != CFile::kNoFile && self->closeOnDelete == TRUE,
          "\"%.80s\" %#x", path, flags);
    return result;
}

void __fastcall DestructVerify(CFile* self) {
    self->Destruct();
    Check("CFile::~CFile", true, self->vtable == CFile::Vtable(), "");
}

CFile* __fastcall DeletingDestructorVerify(CFile* self, void*, unsigned flags) {
    CFile* result = self->DeletingDestructor(flags);
    Check("CFile deleting destructor", true, result == self, "flags %u", flags);
    return result;
}

// 007011E8: PUSH EBP; MOV EBP,ESP; SUB ESP,0x110 (9 bytes). The original opens on a twin first: its
// handle is closed before ours opens (an exclusive share would refuse a second open, so ours cannot
// open first). What it left is kept in our memory and its strings are freed (the exception's name
// first, the file's name last) before ours runs, so no block of the original's is alive while ours
// allocates (SV2h1).
BOOL(__fastcall* s_open)(CFile*, void*, const char*, unsigned, CFileException*);
BOOL __fastcall OpenVerify(CFile* self, void*, const char* path, unsigned flags, CFileException* error) {
    struct Opened {
        BOOL result;
        BOOL closeOnDelete;
        bool hasHandle;
        std::string fileName;
        int cause;
        LONG osError;
        std::string errorName;
    } expected{};
    {
        Slot<CFile> twin;
        twin.Get()->Construct();
        Slot<CFileException> twinError;
        if (error) {
            std::memcpy(twinError.Get(), error, sizeof(CFileException));
            ::new (&twinError.Get()->fileName) CString(error->fileName);
        }
        expected.result = s_open(twin.Get(), nullptr, path, flags, error ? twinError.Get() : nullptr);
        if (twin.Get()->handle != CFile::kNoFile) gameapi::CloseHandle(twin.Get()->handle);
        expected.closeOnDelete = twin.Get()->closeOnDelete;
        expected.hasHandle = twin.Get()->handle != CFile::kNoFile;
        expected.fileName = Text(twin.Get()->fileName);
        if (error) {
            expected.cause = twinError.Get()->cause;
            expected.osError = twinError.Get()->osError;
            expected.errorName = Text(twinError.Get()->fileName);
            twinError.Get()->fileName.~CString();
        }
        twin.Get()->fileName.~CString();
    }
    const BOOL result = self->Open(path, flags, error);
    bool ok = result == expected.result && self->closeOnDelete == expected.closeOnDelete && expected.fileName == Text(self->fileName) &&
              (self->handle != CFile::kNoFile) == expected.hasHandle;
    if (error)
        ok = ok && error->cause == expected.cause && error->osError == expected.osError && expected.errorName == Text(error->fileName);
    Check("CFile::Open", true, ok, "\"%.80s\" %#x: %d \"%.80s\" (original %d \"%.80s\")", path, flags, result, Text(self->fileName),
          expected.result, expected.fileName.c_str());
    return result;
}

// 007013B9: PUSH ESI; PUSH 1; PUSH 0 (5 bytes).
unsigned long(__fastcall* s_getPosition)(const CFile*);
unsigned long __fastcall GetPositionVerify(const CFile* self) {
    const unsigned long expected = s_getPosition(self);
    const unsigned long actual = self->GetPosition();
    Check("CFile::GetPosition", expected, actual, "");
    return actual;
}

// 007014E2: PUSH EBX; PUSH ESI; MOV ESI,ECX; PUSH EDI (5 bytes).
unsigned long(__fastcall* s_getLength)(const CFile*);
unsigned long __fastcall GetLengthVerify(const CFile* self) {
    const unsigned long expected = s_getLength(self);
    const unsigned long actual = self->GetLength();
    Check("CFile::GetLength", expected, actual, "");
    return actual;
}

// 00701305: PUSH EBP; MOV EBP,ESP; CMP [EBP+0xC],0 (7 bytes). The original reads into scratch, the
// file goes back, ours reads into the caller's buffer.
unsigned(__fastcall* s_read)(CFile*, void*, void*, unsigned);
unsigned __fastcall ReadVerify(CFile* self, void*, void* buffer, unsigned count) {
    const DWORD at = FilePosition(self->handle);
    if (count == 0 || at == INVALID_SET_FILE_POINTER) return self->Read(buffer, count);
    std::vector<unsigned char> scratch(count);
    const unsigned expected = s_read(self, nullptr, scratch.data(), count);
    const DWORD expectedEnd = FilePosition(self->handle);
    gameapi::SetFilePointer(self->handle, static_cast<LONG>(at), nullptr, FILE_BEGIN);
    const unsigned actual = self->Read(buffer, count);
    const DWORD end = FilePosition(self->handle);
    Check("CFile::Read", true, actual == expected && end == expectedEnd && std::memcmp(buffer, scratch.data(), actual) == 0,
          "%u of %u at %lu (original %u)", actual, count, at, expected);
    return actual;
}

void __fastcall WriteVerify(CFile* self, void*, const void* buffer, unsigned count) {
    const DWORD at = FilePosition(self->handle);
    self->Write(buffer, count);
    const DWORD end = FilePosition(self->handle);
    Check("CFile::Write", true, at == INVALID_SET_FILE_POINTER || end == at + count, "%u at %lu: %lu", count, at, end);
}

long __fastcall SeekVerify(CFile* self, void*, long offset, unsigned from) {
    const long result = self->Seek(offset, from);
    Check("CFile::Seek", true, static_cast<DWORD>(result) == FilePosition(self->handle), "%ld from %u: %ld", offset, from, result);
    return result;
}

void __fastcall SetLengthVerify(CFile* self, void*, unsigned long length) {
    self->SetLength(length);
    Check("CFile::SetLength", length, static_cast<unsigned long>(gameapi::GetFileSize(self->handle, nullptr)), "");
}

void __fastcall SetFilePathVerify(CFile* self, void*, const char* path) {
    self->SetFilePath(path);
    Check("CFile::SetFilePath", true, std::strcmp(Text(self->fileName), path ? path : "") == 0, "");
}

CFile* __fastcall DuplicateVerify(const CFile* self) {
    CFile* copy = self->Duplicate();
    Check("CFile::Duplicate", true, copy && copy->vtable == CFile::Vtable() && copy->handle != CFile::kNoFile &&
                                        copy->closeOnDelete == self->closeOnDelete, "");
    return copy;
}

void __fastcall LockRangeVerify(CFile* self, void*, unsigned long position, unsigned long count) {
    self->LockRange(position, count);
    Check("CFile::LockRange", true, true, "");
}

void __fastcall UnlockRangeVerify(CFile* self, void*, unsigned long position, unsigned long count) {
    self->UnlockRange(position, count);
    Check("CFile::UnlockRange", true, true, "");
}

void __fastcall AbortVerify(CFile* self) {
    self->Abort();
    Check("CFile::Abort", true, self->handle == CFile::kNoFile && self->fileName.GetLength() == 0, "");
}

void __fastcall FlushVerify(CFile* self) {
    self->Flush();
    Check("CFile::Flush", true, true, "");
}

void __fastcall CloseVerify(CFile* self) {
    self->Close();
    Check("CFile::Close", true, self->handle == CFile::kNoFile && self->closeOnDelete == FALSE && self->fileName.GetLength() == 0, "");
}

unsigned __fastcall GetBufferPtrVerify(CFile* self, void*, unsigned command, unsigned count, void** start, void** end) {
    const unsigned result = self->GetBufferPtr(command, count, start, end);
    Check("CFile::GetBufferPtr", 0u, result, "");
    return result;
}

// ---- CStdioFile ------------------------------------------------------------------------------------

long Tell(CStdioFile* self) { return self->stream ? gamecrt::ftell(self->stream) : -1; }

CStdioFile* __fastcall StdioConstructVerify(CStdioFile* self) {
    CStdioFile* result = self->Construct();
    Check("CStdioFile::CStdioFile()", true, result == self && self->vtable == CStdioFile::Vtable() && self->stream == nullptr &&
                                                self->handle == CFile::kNoFile && self->closeOnDelete == FALSE, "");
    return result;
}

CStdioFile* __fastcall StdioConstructOpenVerify(CStdioFile* self, void*, const char* path, unsigned flags) {
    CStdioFile* result = self->ConstructOpen(path, flags);
    Check("CStdioFile::CStdioFile(path, flags)", true, result == self && self->vtable == CStdioFile::Vtable() && self->stream != nullptr,
          "\"%.80s\" %#x", path, flags);
    return result;
}

void __fastcall StdioDestructVerify(CStdioFile* self) {
    self->Destruct();
    Check("CStdioFile::~CStdioFile", true, self->vtable == CFile::Vtable(), "");
}

CStdioFile* __fastcall StdioDeletingDestructorVerify(CStdioFile* self, void*, unsigned flags) {
    CStdioFile* result = self->DeletingDestructor(flags);
    Check("CStdioFile deleting destructor", true, result == self, "flags %u", flags);
    return result;
}

BOOL __fastcall StdioOpenVerify(CStdioFile* self, void*, const char* path, unsigned flags, CFileException* error) {
    const BOOL result = self->Open(path, flags, error);
    Check("CStdioFile::Open", true, result ? self->stream != nullptr : self->stream == nullptr && self->handle == CFile::kNoFile,
          "\"%.80s\" %#x: %d", path, flags, result);
    return result;
}

unsigned long __fastcall StdioGetPositionVerify(const CStdioFile* self) {
    const unsigned long result = self->GetPosition();
    Check("CStdioFile::GetPosition", static_cast<unsigned long>(Tell(const_cast<CStdioFile*>(self))), result, "");
    return result;
}

long __fastcall StdioSeekVerify(CStdioFile* self, void*, long offset, unsigned from) {
    const long result = self->Seek(offset, from);
    Check("CStdioFile::Seek", Tell(self), result, "%ld from %u", offset, from);
    return result;
}

unsigned __fastcall StdioReadVerify(CStdioFile* self, void*, void* buffer, unsigned count) {
    const unsigned result = self->Read(buffer, count);
    Check("CStdioFile::Read", true, result <= count, "%u of %u", result, count);
    return result;
}

void __fastcall StdioWriteVerify(CStdioFile* self, void*, const void* buffer, unsigned count) {
    self->Write(buffer, count);
    Check("CStdioFile::Write", true, true, "");
}

void __fastcall StdioWriteStringVerify(CStdioFile* self, void*, const char* text) {
    self->WriteString(text);
    Check("CStdioFile::WriteString", true, true, "");
}

// 00701E93: PUSH EBX/EBP/ESI/EDI; MOV EDI,[ESP+0x14] (8 bytes). Read twice: ours into the caller's
// string, back to the start (fseek), the original into a scratch string; the stream is put back
// where ours left it when the original ended elsewhere (SV2h1).
BOOL(__fastcall* s_readLine)(CStdioFile*, void*, CString&);
BOOL __fastcall StdioReadLineVerify(CStdioFile* self, void*, CString& text) {
    const long at = Tell(self);
    if (at < 0) return self->ReadLine(text);
    const BOOL result = self->ReadLine(text);
    const long end = Tell(self);
    gamecrt::fseek(self->stream, at, SEEK_SET);
    CString expected;
    const BOOL expectedResult = s_readLine(self, nullptr, expected);
    const long expectedEnd = Tell(self);
    if (expectedEnd != end) gamecrt::fseek(self->stream, end, SEEK_SET);
    Check("CStdioFile::ReadString(CString&)", true, result == expectedResult && Same(text, expected) && end == expectedEnd,
          "%d \"%.80s\" (original %d \"%.80s\")", result, Text(text), expectedResult, Text(expected));
    return result;
}

// 00701E4E: PUSH ESI; MOV ESI,ECX; PUSH EDI; PUSH [ESI+0x10] (7 bytes).
char*(__fastcall* s_readString)(CStdioFile*, void*, char*, unsigned);
char* __fastcall StdioReadStringVerify(CStdioFile* self, void*, char* buffer, unsigned size) {
    const long at = Tell(self);
    if (at < 0 || size == 0) return self->ReadString(buffer, size);
    std::vector<char> scratch(size);
    const char* expected = s_readString(self, nullptr, scratch.data(), size);
    const long expectedEnd = Tell(self);
    gamecrt::fseek(self->stream, at, SEEK_SET);
    char* result = self->ReadString(buffer, size);
    const bool ok = (result != nullptr) == (expected != nullptr) && Tell(self) == expectedEnd &&
                    (!result || std::strcmp(buffer, scratch.data()) == 0);
    Check("CStdioFile::ReadString(char*, unsigned)", true, ok, "%u", size);
    return result;
}

void __fastcall StdioAbortVerify(CStdioFile* self) {
    self->Abort();
    Check("CStdioFile::Abort", true, self->stream == nullptr && self->handle == CFile::kNoFile, "");
}

void __fastcall StdioFlushVerify(CStdioFile* self) {
    self->Flush();
    Check("CStdioFile::Flush", true, true, "");
}

void __fastcall StdioCloseVerify(CStdioFile* self) {
    self->Close();
    Check("CStdioFile::Close", true, self->stream == nullptr && self->handle == CFile::kNoFile && self->closeOnDelete == FALSE, "");
}

// Not supported: they throw.
CFile* __fastcall StdioDuplicateVerify(const CStdioFile* self) {
    Check("CStdioFile::Duplicate", true, true, "");
    return self->Duplicate();
}

void __fastcall StdioLockRangeVerify(CStdioFile* self, void*, unsigned long position, unsigned long count) {
    Check("CStdioFile::LockRange", true, true, "");
    self->LockRange(position, count);
}

// ---- CFileFind -------------------------------------------------------------------------------------

// 006E6370: MOV EAX,ECX; MOV ECX,[afxEmptyString] (8 bytes).
CFileFind*(__fastcall* s_findConstruct)(CFileFind*);
CFileFind* __fastcall FindConstructVerify(CFileFind* self) {
    Slot<CFileFind> twin;
    s_findConstruct(twin.Get());
    CFileFind* result = self->Construct();
    const CFileFind& t = *twin.Get();
    Check("CFileFind::CFileFind", true, result == self && t.vtable == self->vtable && t.found == self->found && t.next == self->next &&
                                            t.context == self->context && t.separator == self->separator && Same(t.root, self->root), "");
    twin.Get()->root.~CString();
    return result;
}

void __fastcall FindDestructVerify(CFileFind* self) {
    self->Destruct();
    Check("CFileFind::~CFileFind", true, self->vtable == CFileFind::Vtable() && !self->found && !self->next, "");
}

CFileFind* __fastcall FindDeletingDestructorVerify(CFileFind* self, void*, unsigned flags) {
    CFileFind* result = self->DeletingDestructor(flags);
    Check("CFileFind deleting destructor", true, result == self, "flags %u", flags);
    return result;
}

void __fastcall FindCloseVerify(CFileFind* self) {
    self->Close();
    Check("CFileFind::Close", true, !self->found && !self->next && (!self->context || self->context == INVALID_HANDLE_VALUE), "");
}

BOOL __fastcall FindCloseContextVerify(CFileFind* self) {
    const BOOL result = self->CloseContext();
    Check("CFileFind::CloseContext", true, true, "");
    return result;
}

BOOL __fastcall FindFileVerify(CFileFind* self, void*, const char* pattern, unsigned long flags) {
    const BOOL result = self->FindFile(pattern, flags);
    Check("CFileFind::FindFile", true,
          result ? self->next != nullptr && self->context != INVALID_HANDLE_VALUE && !self->found : !self->found && !self->next,
          "\"%.80s\": %d", pattern ? pattern : "(null)", result);
    return result;
}

BOOL __fastcall FindNextFileVerify(CFileFind* self) {
    const bool searching = self->context != nullptr;
    const BOOL result = self->FindNextFile();
    Check("CFileFind::FindNextFile", true, !searching ? result == FALSE : self->next != nullptr, "%d", result);
    return result;
}

// The string getters: ours into the caller's storage, then the original into scratch storage. GetFileName / GetFilePath / GetFileTitle /
// GetFileURL: MOV EAX,imm32 (5 bytes); GetRoot: PUSH EBP; MOV EBP,ESP; PUSH ECX; AND [EBP-4],0 (8).
using StringGetter = CString*(__fastcall*)(const CFileFind*, void*, CString*);
template <int Id>
struct FindString {
    static inline StringGetter original;
    static inline CString (CFileFind::*ours)() const;
    static inline const char* name;
    static CString* __fastcall Verify(const CFileFind* self, void*, CString* result) {
        ::new (result) CString((self->*ours)());
        Slot<CString> expected;
        original(self, nullptr, expected.Get());
        Check(name, true, Same(*expected.Get(), *result), "\"%.80s\" (original \"%.80s\")", Text(*result), Text(*expected.Get()));
        expected.Get()->~CString();
        return result;
    }
};
using FileName = FindString<0>;
using FilePath = FindString<1>;
using FileTitle = FindString<2>;
using FileUrl = FindString<3>;
using Root = FindString<4>;

// MatchesMask: MOV EAX,[ECX+4]; TEST EAX,EAX (5 bytes). IsDots: PUSH ESI; PUSH EDI; MOV ESI,ECX;
// XOR EDI,EDI (6). GetLength: as MatchesMask (5).
BOOL(__fastcall* s_matchesMask)(const CFileFind*, void*, unsigned long);
BOOL __fastcall MatchesMaskVerify(const CFileFind* self, void*, unsigned long mask) {
    const BOOL expected = s_matchesMask(self, nullptr, mask);
    const BOOL actual = self->MatchesMask(mask);
    Check("CFileFind::MatchesMask", expected, actual, "%#lx", mask);
    return actual;
}

BOOL(__fastcall* s_isDots)(const CFileFind*);
BOOL __fastcall IsDotsVerify(const CFileFind* self) {
    const BOOL expected = s_isDots(self);
    const BOOL actual = self->IsDots();
    Check("CFileFind::IsDots", expected, actual, "");
    return actual;
}

unsigned long(__fastcall* s_findLength)(const CFileFind*);
unsigned long __fastcall FindLengthVerify(const CFileFind* self) {
    const unsigned long expected = s_findLength(self);
    const unsigned long actual = self->GetLength();
    Check("CFileFind::GetLength", expected, actual, "");
    return actual;
}

// The FILETIME getters: MOV EAX,[ECX+4]; TEST EAX,EAX (5 bytes); the CTime ones: PUSH EBP; MOV EBP,ESP;
// PUSH ECX; MOV EAX,[ECX+4] (7).
template <class T, int Id>
struct FindTime {
    static inline BOOL(__fastcall* original)(const CFileFind*, void*, T*);
    static inline BOOL (CFileFind::*ours)(T*) const;
    static inline const char* name;
    static BOOL __fastcall Verify(const CFileFind* self, void*, T* time) {
        T expected{};
        const BOOL expectedResult = original(self, nullptr, time ? &expected : nullptr);
        const BOOL result = (self->*ours)(time);
        Check(name, true, result == expectedResult && (!result || !time || std::memcmp(&expected, time, sizeof(T)) == 0), "%d (original %d)",
              result, expectedResult);
        return result;
    }
};
using WriteFileTime = FindTime<FILETIME, 0>;
using AccessFileTime = FindTime<FILETIME, 1>;
using CreationFileTime = FindTime<FILETIME, 2>;
using WriteTime = FindTime<CTime, 0>;
using AccessTime = FindTime<CTime, 1>;
using CreationTime = FindTime<CTime, 2>;

// ---- CTime -----------------------------------------------------------------------------------------

// The constructors: PUSH EBP; MOV EBP,ESP; MOV EAX,[EBP+n] or SUB ESP,n (6 bytes).
CTime*(__fastcall* s_timeConstruct)(CTime*, void*, int, int, int, int, int, int, int);
CTime* __fastcall TimeConstructVerify(CTime* self, void*, int year, int month, int day, int hour, int minute, int second, int dst) {
    CTime expected{};
    s_timeConstruct(&expected, nullptr, year, month, day, hour, minute, second, dst);
    CTime* result = self->Construct(year, month, day, hour, minute, second, dst);
    Check("CTime::CTime(y, m, d, h, m, s)", expected.time, self->time, "%d-%d-%d %d:%d:%d %d", year, month, day, hour, minute, second, dst);
    return result;
}

CTime*(__fastcall* s_timeSystem)(CTime*, void*, const SYSTEMTIME&, int);
CTime* __fastcall TimeSystemVerify(CTime* self, void*, const SYSTEMTIME& at, int dst) {
    CTime expected{};
    s_timeSystem(&expected, nullptr, at, dst);
    CTime* result = self->ConstructSystemTime(at, dst);
    Check("CTime::CTime(SYSTEMTIME)", expected.time, self->time, "%d-%d-%d %d", at.wYear, at.wMonth, at.wDay, dst);
    return result;
}

CTime*(__fastcall* s_timeFile)(CTime*, void*, const FILETIME&, int);
CTime* __fastcall TimeFileVerify(CTime* self, void*, const FILETIME& at, int dst) {
    CTime expected{};
    s_timeFile(&expected, nullptr, at, dst);
    CTime* result = self->ConstructFileTime(at, dst);
    Check("CTime::CTime(FILETIME)", expected.time, self->time, "%08lx%08lx %d", at.dwHighDateTime, at.dwLowDateTime, dst);
    return result;
}

CTime* __stdcall CurrentTimeVerify(CTime* out) {
    CTime* result = CTime::CurrentTime(out);
    Check("CTime::GetCurrentTime", true, result == out && out->time > 0, "%ld", out->time);
    return result;
}

// 006FE795: PUSH EBP; MOV EBP,ESP; SUB ESP,0x84 (9 bytes).
CString*(__fastcall* s_timeFormat)(const CTime*, void*, CString*, const char*);
CString* __fastcall TimeFormatVerify(const CTime* self, void*, CString* result, const char* format) {
    ::new (result) CString(self->Format(format));
    Slot<CString> expected;
    s_timeFormat(self, nullptr, expected.Get(), format);
    Check("CTime::Format", true, Same(*expected.Get(), *result), "\"%.40s\": \"%.80s\" (original \"%.80s\")", format, Text(*result),
          Text(*expected.Get()));
    expected.Get()->~CString();
    return result;
}

// ---- Start-up check --------------------------------------------------------------------------------

// The classes through their addresses (the wrappers above).
__declspec(naked) CFile* __fastcall FileConstructOpenAt(CFile*, void*, const char*, unsigned) EI_ORIGINAL(0x0070108B)
__declspec(naked) void __fastcall FileDestructAt(CFile*) EI_ORIGINAL(0x0070112B)
__declspec(naked) void __fastcall FileWriteAt(CFile*, void*, const void*, unsigned) EI_ORIGINAL(0x0070133F)
__declspec(naked) unsigned __fastcall FileReadAt(CFile*, void*, void*, unsigned) EI_ORIGINAL(0x00701305)
__declspec(naked) long __fastcall FileSeekAt(CFile*, void*, long, unsigned) EI_ORIGINAL(0x0070138A)
__declspec(naked) unsigned long __fastcall FileGetLengthAt(const CFile*) EI_ORIGINAL(0x007014E2)
__declspec(naked) void __fastcall FileSetLengthAt(CFile*, void*, unsigned long) EI_ORIGINAL(0x007014B6)
__declspec(naked) void __fastcall FileCloseAt(CFile*) EI_ORIGINAL(0x00701403)
__declspec(naked) CStdioFile* __fastcall StdioConstructOpenAt(CStdioFile*, void*, const char*, unsigned) EI_ORIGINAL(0x00701BD4)
__declspec(naked) void __fastcall StdioDestructAt(CStdioFile*) EI_ORIGINAL(0x00701C6D)
__declspec(naked) void __fastcall StdioWriteStringAt(CStdioFile*, void*, const char*) EI_ORIGINAL(0x00701E23)
__declspec(naked) BOOL __fastcall StdioReadLineAt(CStdioFile*, void*, CString&) EI_ORIGINAL(0x00701E93)
__declspec(naked) void __fastcall StdioCloseAt(CStdioFile*) EI_ORIGINAL(0x00701FE0)
__declspec(naked) CFileFind* __fastcall FindConstructAt(CFileFind*) EI_ORIGINAL(0x006E6370)
__declspec(naked) void __fastcall FindDestructAt(CFileFind*) EI_ORIGINAL(0x006E63AD)
__declspec(naked) BOOL __fastcall FindFileAt(CFileFind*, void*, const char*, unsigned long) EI_ORIGINAL(0x006E6433)
__declspec(naked) BOOL __fastcall FindNextFileAt(CFileFind*) EI_ORIGINAL(0x006E6653)
__declspec(naked) CString* __fastcall FindFileNameAt(const CFileFind*, void*, CString*) EI_ORIGINAL(0x006E684B)
__declspec(naked) CString* __fastcall FindFilePathAt(const CFileFind*, void*, CString*) EI_ORIGINAL(0x006E6722)
__declspec(naked) BOOL __fastcall FindIsDotsAt(const CFileFind*) EI_ORIGINAL(0x006E661A)
__declspec(naked) void __fastcall DeleteExceptionAt(void*) EI_ORIGINAL(0x00702069)

// The lines written and read back: one longer than ReadString's 128-character steps.
bool LinesCheck(const char* path) {
    Slot<CStdioFile> file;
    StdioConstructOpenAt(file.Get(), nullptr, path, CFile::modeCreate | CFile::modeWrite | CFile::typeText);
    std::string longLine(300, 'x');
    StdioWriteStringAt(file.Get(), nullptr, "line one\n");
    StdioWriteStringAt(file.Get(), nullptr, (longLine + "\n").c_str());
    StdioWriteStringAt(file.Get(), nullptr, "last");
    StdioCloseAt(file.Get());
    StdioDestructAt(file.Get());

    StdioConstructOpenAt(file.Get(), nullptr, path, CFile::modeRead | CFile::typeText);
    CString line;
    bool ok = StdioReadLineAt(file.Get(), nullptr, line) && std::strcmp(Text(line), "line one") == 0;
    ok = ok && StdioReadLineAt(file.Get(), nullptr, line) && longLine == Text(line);
    ok = ok && StdioReadLineAt(file.Get(), nullptr, line) && std::strcmp(Text(line), "last") == 0;
    ok = ok && !StdioReadLineAt(file.Get(), nullptr, line);
    StdioDestructAt(file.Get());   // closes the stream
    return ok;
}

bool BinaryCheck(const char* path) {
    Slot<CFile> file;
    FileConstructOpenAt(file.Get(), nullptr, path, CFile::modeCreate | CFile::modeReadWrite | CFile::shareExclusive);
    const unsigned char bytes[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    FileWriteAt(file.Get(), nullptr, bytes, sizeof(bytes));
    bool ok = FileGetLengthAt(file.Get()) == 10;
    ok = ok && FileSeekAt(file.Get(), nullptr, 2, FILE_BEGIN) == 2;
    unsigned char read[4] = {};
    ok = ok && FileReadAt(file.Get(), nullptr, read, 4) == 4 && std::memcmp(read, bytes + 2, 4) == 0;
    FileSetLengthAt(file.Get(), nullptr, 5);
    ok = ok && FileGetLengthAt(file.Get()) == 5;
    FileCloseAt(file.Get());
    FileDestructAt(file.Get());
    return ok;
}

// The folder holds a.txt and b.bin (and its "." and "..").
bool SearchCheck(const std::string& folder) {
    Slot<CFileFind> find;
    FindConstructAt(find.Get());
    int files = 0, dots = 0;
    bool pathOk = false;
    BOOL more = FindFileAt(find.Get(), nullptr, (folder + "\\*.*").c_str(), 0);
    const bool found = more != FALSE;
    while (more) {
        more = FindNextFileAt(find.Get());
        Slot<CString> name;
        FindFileNameAt(find.Get(), nullptr, name.Get());
        if (FindIsDotsAt(find.Get())) {
            ++dots;
        } else if (std::strcmp(Text(*name.Get()), "a.txt") == 0 || std::strcmp(Text(*name.Get()), "b.bin") == 0) {
            ++files;
            if (std::strcmp(Text(*name.Get()), "a.txt") == 0) {
                Slot<CString> path;
                FindFilePathAt(find.Get(), nullptr, path.Get());
                pathOk = _stricmp(Text(*path.Get()), (folder + "\\a.txt").c_str()) == 0;
                path.Get()->~CString();
            }
        }
        name.Get()->~CString();
    }
    FindDestructAt(find.Get());
    return found && files == 2 && dots == 2 && pathOk;
}

// A file in a folder that does not exist: CFile's constructor throws badPath.
bool MissingCheck(const std::string& folder) {
    Slot<CFile> file;
    int cause = -1;
    try {
        FileConstructOpenAt(file.Get(), nullptr, (folder + "\\missing\\x.bin").c_str(), CFile::modeRead);
        FileDestructAt(file.Get());
    } catch (::CFileException* thrown) {
        cause = reinterpret_cast<CFileException*>(thrown)->cause;
        DeleteExceptionAt(thrown);
    }
    return cause == CFileException::badPath;
}

void FileStartupCheck() {
    char temp[MAX_PATH];
    if (!::GetTempPathA(MAX_PATH, temp)) return;
    char name[64];
    std::snprintf(name, sizeof(name), "ei_mfc_file_%lu", ::GetCurrentProcessId());
    const std::string folder = std::string(temp) + name;
    if (!gameapi::CreateDirectoryA(folder.c_str(), nullptr)) return;
    const std::string text = folder + "\\a.txt";
    const std::string binary = folder + "\\b.bin";
    bool lines = false, bytes = false, search = false, missing = false;
    try {
        lines = LinesCheck(text.c_str());
        bytes = BinaryCheck(binary.c_str());
        search = SearchCheck(folder);
        missing = MissingCheck(folder);
    } catch (::CException* thrown) {
        DeleteExceptionAt(thrown);
    }
    gameapi::DeleteFileA(text.c_str());
    gameapi::DeleteFileA(binary.c_str());
    gameapi::RemoveDirectoryA(folder.c_str());
    Check("CStdioFile lines (start-up)", true, lines, "");
    Check("CFile bytes (start-up)", true, bytes, "");
    Check("CFileFind search (start-up)", true, search, "");
    Check("CFile missing file throws (start-up)", true, missing, "");
}
EI_STARTUP_CHECK(FileStartupCheck);

struct Names {
    Names() {
        FileClass::name = "CFile::GetRuntimeClass";
        FileClass::ours = &mfc::impl::FileRuntimeClass;
        StdioFileClass::name = "CStdioFile::GetRuntimeClass";
        StdioFileClass::ours = &mfc::impl::StdioFileRuntimeClass;
        FileFindClass::name = "CFileFind::GetRuntimeClass";
        FileFindClass::ours = &mfc::impl::FileFindRuntimeClass;
        FileName::name = "CFileFind::GetFileName";
        FileName::ours = &CFileFind::GetFileName;
        FilePath::name = "CFileFind::GetFilePath";
        FilePath::ours = &CFileFind::GetFilePath;
        FileTitle::name = "CFileFind::GetFileTitle";
        FileTitle::ours = &CFileFind::GetFileTitle;
        FileUrl::name = "CFileFind::GetFileURL";
        FileUrl::ours = &CFileFind::GetFileURL;
        Root::name = "CFileFind::GetRoot";
        Root::ours = &CFileFind::GetRoot;
        WriteFileTime::name = "CFileFind::GetLastWriteTime(FILETIME*)";
        WriteFileTime::ours = &CFileFind::GetLastWriteFileTime;
        AccessFileTime::name = "CFileFind::GetLastAccessTime(FILETIME*)";
        AccessFileTime::ours = &CFileFind::GetLastAccessFileTime;
        CreationFileTime::name = "CFileFind::GetCreationTime(FILETIME*)";
        CreationFileTime::ours = &CFileFind::GetCreationFileTime;
        WriteTime::name = "CFileFind::GetLastWriteTime(CTime&)";
        WriteTime::ours = &CFileFind::GetLastWriteTime;
        AccessTime::name = "CFileFind::GetLastAccessTime(CTime&)";
        AccessTime::ours = &CFileFind::GetLastAccessTime;
        CreationTime::name = "CFileFind::GetCreationTime(CTime&)";
        CreationTime::ours = &CFileFind::GetCreationTime;
    }
} s_names;

} // namespace

EI_VERIFY(0x00701870, FullPathVerify, 5, s_fullPath);
EI_VERIFY(0x00701940, GetRootVerify, 5, s_getRoot);
EI_VERIFY(0x0070BE58, FileClass::Verify, 5, FileClass::original);
EI_VERIFY(0x0070C15B, StdioFileClass::Verify, 5, StdioFileClass::original);
EI_VERIFY(0x0070BE5E, FileFindClass::Verify, 5, FileFindClass::original);

EI_VERIFY(0x00701036, ConstructVerify, 8, s_construct);
EI_VERIFY(0x0070106C, ConstructHandleVerify, 8, s_constructHandle);
EI_VERIFY(0x0070108B, ConstructOpenVerify, 0, s_unused);
EI_VERIFY(0x0070112B, DestructVerify, 0, s_unused);
EI_VERIFY(0x00701050, DeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x007011E8, OpenVerify, 9, s_open);
EI_VERIFY(0x007013B9, GetPositionVerify, 5, s_getPosition);
EI_VERIFY(0x007014E2, GetLengthVerify, 5, s_getLength);
EI_VERIFY(0x00701305, ReadVerify, 7, s_read);
EI_VERIFY(0x0070133F, WriteVerify, 0, s_unused);
EI_VERIFY(0x0070138A, SeekVerify, 0, s_unused);
EI_VERIFY(0x007014B6, SetLengthVerify, 0, s_unused);
EI_VERIFY(0x006E6361, SetFilePathVerify, 0, s_unused);
EI_VERIFY(0x00701173, DuplicateVerify, 0, s_unused);
EI_VERIFY(0x00701464, LockRangeVerify, 0, s_unused);
EI_VERIFY(0x0070148D, UnlockRangeVerify, 0, s_unused);
EI_VERIFY(0x00701444, AbortVerify, 0, s_unused);
EI_VERIFY(0x007013E2, FlushVerify, 0, s_unused);
EI_VERIFY(0x00701403, CloseVerify, 0, s_unused);
EI_VERIFY(0x0070150F, GetBufferPtrVerify, 0, s_unused);

EI_VERIFY(0x00701B79, StdioConstructVerify, 0, s_unused);
EI_VERIFY(0x00701BD4, StdioConstructOpenVerify, 0, s_unused);
EI_VERIFY(0x00701C6D, StdioDestructVerify, 0, s_unused);
EI_VERIFY(0x00701B8F, StdioDeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x00701CB0, StdioOpenVerify, 0, s_unused);
EI_VERIFY(0x00701F8E, StdioGetPositionVerify, 0, s_unused);
EI_VERIFY(0x00701F56, StdioSeekVerify, 0, s_unused);
EI_VERIFY(0x00701D84, StdioReadVerify, 0, s_unused);
EI_VERIFY(0x00701DF0, StdioWriteVerify, 0, s_unused);
EI_VERIFY(0x00701E23, StdioWriteStringVerify, 0, s_unused);
EI_VERIFY(0x00701E93, StdioReadLineVerify, 8, s_readLine);
EI_VERIFY(0x00701E4E, StdioReadStringVerify, 7, s_readString);
EI_VERIFY(0x00702016, StdioAbortVerify, 0, s_unused);
EI_VERIFY(0x00701FB8, StdioFlushVerify, 0, s_unused);
EI_VERIFY(0x00701FE0, StdioCloseVerify, 0, s_unused);
EI_VERIFY(0x0070203B, StdioDuplicateVerify, 0, s_unused);
EI_VERIFY(0x00702043, StdioLockRangeVerify, 0, s_unused);
EI_VERIFY(0x0070204B, StdioLockRangeVerify, 0, s_unused);

EI_VERIFY(0x006E6370, FindConstructVerify, 8, s_findConstruct);
EI_VERIFY(0x006E63AD, FindDestructVerify, 0, s_unused);
EI_VERIFY(0x006E6391, FindDeletingDestructorVerify, 0, s_unused);
EI_VERIFY(0x006E63E9, FindCloseVerify, 0, s_unused);
EI_VERIFY(0x006E6429, FindCloseContextVerify, 0, s_unused);
EI_VERIFY(0x006E6433, FindFileVerify, 0, s_unused);
EI_VERIFY(0x006E6653, FindNextFileVerify, 0, s_unused);
EI_VERIFY(0x006E684B, FileName::Verify, 5, FileName::original);
EI_VERIFY(0x006E6722, FilePath::Verify, 5, FilePath::original);
EI_VERIFY(0x006E67BA, FileTitle::Verify, 5, FileTitle::original);
EI_VERIFY(0x006E668E, FileUrl::Verify, 5, FileUrl::original);
EI_VERIFY(0x006E6707, Root::Verify, 8, Root::original);
EI_VERIFY(0x006E6513, MatchesMaskVerify, 5, s_matchesMask);
EI_VERIFY(0x006E661A, IsDotsVerify, 6, s_isDots);
EI_VERIFY(0x006E68AA, FindLengthVerify, 5, s_findLength);
EI_VERIFY(0x006E6551, WriteFileTime::Verify, 5, WriteFileTime::original);
EI_VERIFY(0x006E652D, AccessFileTime::Verify, 5, AccessFileTime::original);
EI_VERIFY(0x006E6575, CreationFileTime::Verify, 5, CreationFileTime::original);
EI_VERIFY(0x006E65C4, WriteTime::Verify, 7, WriteTime::original);
EI_VERIFY(0x006E6599, AccessTime::Verify, 7, AccessTime::original);
EI_VERIFY(0x006E65EF, CreationTime::Verify, 7, CreationTime::original);

EI_VERIFY(0x006FE3BB, TimeConstructVerify, 6, s_timeConstruct);
EI_VERIFY(0x006FE470, TimeSystemVerify, 6, s_timeSystem);
EI_VERIFY(0x006FE4BC, TimeFileVerify, 6, s_timeFile);
EI_VERIFY(0x006FE508, CurrentTimeVerify, 0, s_unused);
EI_VERIFY(0x006FE795, TimeFormatVerify, 9, s_timeFormat);
