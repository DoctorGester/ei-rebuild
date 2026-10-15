// MFC 4.2's CFile, CStdioFile and CFileFind, registered at their library addresses (mfc_file.h).
#include "mfc_file.h"

#include "ei/hook.h"
#include "gameapi.h"
#include "game/originals_library.h"   // MbsInc

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <new>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace mfc::impl {

// CFileFind's vtable slots that its own members call (mfc::CFileFindVirtuals with this file's
// CString); declared only.
class __declspec(novtable) FindSlots {
public:
    virtual void GetRuntimeClass();                                 // 0
    virtual void DeletingDestructor();                              // 1
    virtual void Serialize();                                       // 2
    virtual CString GetFileName() const;                            // 3
    virtual CString GetFilePath() const;                            // 4
    virtual CString GetFileTitle() const;                           // 5
    virtual CString GetFileURL() const;                             // 6
    virtual CString GetRoot() const;                                // 7
    virtual BOOL GetLastWriteTime(CTime* time) const;               // 8
    virtual BOOL GetLastWriteFileTime(FILETIME* time) const;        // 9
    virtual BOOL GetLastAccessTime(CTime* time) const;              // 10
    virtual BOOL GetLastAccessFileTime(FILETIME* time) const;       // 11
    virtual BOOL GetCreationTime(CTime* time) const;                // 12
    virtual BOOL GetCreationFileTime(FILETIME* time) const;         // 13
    virtual BOOL MatchesMask(unsigned long mask) const;             // 14
    virtual BOOL IsDots() const;                                    // 15
    virtual BOOL FindFile(const char* pattern, unsigned long flags);    // 16
    virtual BOOL FindNextFile();                                    // 17
    virtual BOOL CloseContext();                                    // 18
};

namespace {

using uchar = unsigned char;

bool InImage() {
    static const bool standalone = ei::Standalone();
    return standalone;
}

mfc::CFileVirtuals* Virtuals(CFile* file) { return reinterpret_cast<mfc::CFileVirtuals*>(file); }
const mfc::CFileVirtuals* Virtuals(const CFile* file) { return reinterpret_cast<const mfc::CFileVirtuals*>(file); }
const FindSlots* Virtuals(const CFileFind* find) { return reinterpret_cast<const FindSlots*>(find); }
FindSlots* Virtuals(CFileFind* find) { return reinterpret_cast<FindSlots*>(find); }

// The classes' CRuntimeClass records and AfxThrowNotSupportedException's thrown object.
void* FileClass() { return reinterpret_cast<void*>(0x00745DB0); }
void* StdioFileClass() { return reinterpret_cast<void*>(0x007464C8); }
void* FileFindClass() { return reinterpret_cast<void*>(0x00745EC0); }
// "*.*", FindFile's pattern when given none; "file://", GetFileURL's prefix.
const char* AllFiles() { return reinterpret_cast<const char*>(0x007905B4); }
const char* FileUrlPrefix() { return reinterpret_cast<const char*>(0x00745F34); }
// afxChNil: the empty text ReadString starts a line from.
const char* ChNil() { return reinterpret_cast<const char*>(0x007C8BA8); }

// ---- The CRT stream calls of CStdioFile -----------------------------------------------------------
//
// The hook build calls the game CRT (its FILE table is shared with the exe's remaining CRT code); in
// the standalone image the game's FILE* is the host CRT's (crt_file.cpp) and these are the host's.

__declspec(naked) int __cdecl GameOpenOsfhandle(long, int) EI_ORIGINAL(0x006ED1A3)                    // _open_osfhandle
__declspec(naked) gamecrt::CrtFile* __cdecl GameFdopen(int, const char*) EI_ORIGINAL(0x006ED350)      // _fdopen
__declspec(naked) unsigned long* __cdecl GameDoserrno() EI_ORIGINAL(0x006ED347)                      // __doserrno
__declspec(naked) int __cdecl GameFputs(const char*, gamecrt::CrtFile*) EI_ORIGINAL(0x006ED484)      // fputs
__declspec(naked) char* __cdecl GameFgets(char*, int, gamecrt::CrtFile*) EI_ORIGINAL(0x006ED4D2)     // fgets
__declspec(naked) void __cdecl GameClearerr(gamecrt::CrtFile*) EI_ORIGINAL(0x006ED443)               // clearerr
__declspec(naked) char* __cdecl GameFullpath(char*, const char*, std::size_t) EI_ORIGINAL(0x006ECA0E) // _fullpath

FILE* Host(gamecrt::CrtFile* stream) { return reinterpret_cast<FILE*>(stream); }

// The game's FILE::_flag (+0x0C): _IOEOF and _IOERR.
struct GameStream {
    std::uint8_t unused_00[0x0C];
    int flags;
};
constexpr int kStreamEof = 0x10;
constexpr int kStreamError = 0x20;

int OpenOsfhandle(HANDLE file, int flags) {
    if (!InImage()) return GameOpenOsfhandle(reinterpret_cast<long>(file), flags);
    return _open_osfhandle(reinterpret_cast<intptr_t>(file), flags);
}
gamecrt::CrtFile* Fdopen(int fd, const char* mode) {
    if (!InImage()) return GameFdopen(fd, mode);
    return reinterpret_cast<gamecrt::CrtFile*>(_fdopen(fd, mode));
}
long DosErrno() {
    if (!InImage()) return static_cast<long>(*GameDoserrno());
    return static_cast<long>(_doserrno);
}
int Fputs(const char* text, gamecrt::CrtFile* stream) { return InImage() ? std::fputs(text, Host(stream)) : GameFputs(text, stream); }
char* Fgets(char* buffer, int size, gamecrt::CrtFile* stream) {
    return InImage() ? std::fgets(buffer, size, Host(stream)) : GameFgets(buffer, size, stream);
}
void Clearerr(gamecrt::CrtFile* stream) {
    if (InImage()) std::clearerr(Host(stream));
    else GameClearerr(stream);
}
int Fflush(gamecrt::CrtFile* stream) { return InImage() ? std::fflush(Host(stream)) : gamecrt::fflush(stream); }
bool AtEnd(gamecrt::CrtFile* stream) {
    if (InImage()) return std::feof(Host(stream)) != 0;
    return (reinterpret_cast<const GameStream*>(stream)->flags & kStreamEof) != 0;
}
bool Failed(gamecrt::CrtFile* stream) {
    if (InImage()) return std::ferror(Host(stream)) != 0;
    return (reinterpret_cast<const GameStream*>(stream)->flags & kStreamError) != 0;
}
// _fullpath into a caller's buffer: GetFullPathNameA, null when it fails or does not fit (an empty
// path is the current directory).
char* Fullpath(char* out, const char* path, std::size_t size) {
    if (!InImage()) return GameFullpath(out, path, size);
    if (!path || !*path) return _getcwd(out, static_cast<int>(size));
    char* part;
    const DWORD length = gameapi::GetFullPathNameA(path, static_cast<DWORD>(size), out, &part);
    if (length >= size) {
        errno = ERANGE;
        return nullptr;
    }
    if (length == 0) {
        _doserrno = gameapi::GetLastError();
        return nullptr;
    }
    return out;
}

// '\\' or '/'.
bool IsSeparator(char c) { return c == '\\' || c == '/'; }
char* Next(char* p) { return const_cast<char*>(::MbsInc(p)); }

// While armed, destroys a string when the frame unwinds: a constructor that throws leaves the
// members it had built destroyed, as the original's unwind does.
class UnwindString {
public:
    explicit UnwindString(CString* string) : string_(string) {}
    ~UnwindString() {
        if (string_) string_->~CString();
    }
    void Disarm() { string_ = nullptr; }
    UnwindString(const UnwindString&) = delete;
    UnwindString& operator=(const UnwindString&) = delete;

private:
    CString* string_;
};

class UnwindFile {
public:
    explicit UnwindFile(CFile* file) : file_(file) {}
    ~UnwindFile() {
        if (file_) file_->Destruct();
    }
    void Disarm() { file_ = nullptr; }
    UnwindFile(const UnwindFile&) = delete;
    UnwindFile& operator=(const UnwindFile&) = delete;

private:
    CFile* file_;
};

} // namespace

// ---- Paths ----------------------------------------------------------------------------------------

void __stdcall GetRoot(const char* path, CString& root) {
    char* start = root.GetBuffer(MAX_PATH);
    std::memset(start, 0, MAX_PATH);
    gameapi::lstrcpynA(start, path, MAX_PATH);
    char* p = start;
    while (*p != '\0' && !(IsSeparator(p[0]) && IsSeparator(p[1]))) p = Next(p);
    if (*p != '\0') {
        // A UNC name: up to the separator after the share's name.
        p += 2;
        while (*p != '\0' && !IsSeparator(*p)) p = Next(p);
        if (*p != '\0') p = Next(p);
        while (*p != '\0' && !IsSeparator(*p)) p = Next(p);
    } else {
        p = start;
        while (*p != '\0' && !IsSeparator(*p)) p = Next(p);
    }
    if (*p != '\0') p[1] = '\0';
    root.ReleaseBuffer(-1);
}

BOOL __stdcall FullPath(char* out, const char* path) {
    char* filePart;
    if (!gameapi::GetFullPathNameA(path, MAX_PATH, out, &filePart)) {
        gameapi::lstrcpynA(out, path, MAX_PATH);
        return FALSE;
    }
    CString root;
    GetRoot(out, root);
    DWORD unused, flags;
    if (!gameapi::GetVolumeInformationA(root, nullptr, 0, nullptr, &unused, &flags, nullptr, 0)) return FALSE;
    if (!(flags & FS_CASE_IS_PRESERVED)) gameapi::CharUpperA(out);
    if (!(flags & FS_UNICODE_STORED_ON_DISK)) {
        WIN32_FIND_DATAA data;
        const HANDLE find = gameapi::FindFirstFileA(path, &data);
        if (find != INVALID_HANDLE_VALUE) {
            gameapi::FindClose(find);
            gameapi::lstrcpyA(filePart, data.cFileName);
        }
    }
    return TRUE;
}

void* __fastcall FileRuntimeClass(const void*) { return FileClass(); }
void* __fastcall StdioFileRuntimeClass(const void*) { return StdioFileClass(); }
void* __fastcall FileFindRuntimeClass(const void*) { return FileFindClass(); }

// ---- CFile ----------------------------------------------------------------------------------------

CFile* CFile::Construct() {
    new (&fileName) CString();
    handle = kNoFile;
    closeOnDelete = FALSE;
    vtable = Vtable();
    return this;
}

CFile* CFile::ConstructHandle(HANDLE file) {
    new (&fileName) CString();
    closeOnDelete = FALSE;
    vtable = Vtable();
    handle = file;
    return this;
}

CFile* CFile::ConstructOpen(const char* path, unsigned flags) {
    new (&fileName) CString();
    UnwindString unwind(&fileName);
    vtable = Vtable();
    CFileException error;
    error.InitEmpty();
    if (!Open(path, flags, &error)) ThrowFileException(error.cause, error.osError, error.fileName);
    error.vtable = CFileException::Vtable();
    unwind.Disarm();
    return this;
}

void CFile::Destruct() {
    vtable = Vtable();
    if (handle != kNoFile && closeOnDelete) Close();
    fileName.~CString();
}

CFile* CFile::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

BOOL CFile::Open(const char* path, unsigned flags, CFileException* error) {
    closeOnDelete = FALSE;
    handle = kNoFile;
    fileName.Empty();
    flags &= ~typeBinary;
    char full[MAX_PATH];
    FullPath(full, path);
    fileName = full;

    DWORD access = 0;
    switch (flags & 3) {
    case modeRead: access = GENERIC_READ; break;
    case modeWrite: access = GENERIC_WRITE; break;
    case modeReadWrite: access = GENERIC_READ | GENERIC_WRITE; break;
    default: break;
    }
    DWORD share;
    switch (flags & 0x70) {
    case shareDenyWrite: share = FILE_SHARE_READ; break;
    case shareDenyRead: share = FILE_SHARE_WRITE; break;
    case shareDenyNone: share = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
    default: share = 0; break;   // compatibility, exclusive and the undefined values
    }
    SECURITY_ATTRIBUTES security;
    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = nullptr;
    security.bInheritHandle = (flags & modeNoInherit) ? FALSE : TRUE;
    DWORD disposition = OPEN_EXISTING;
    if (flags & modeCreate) disposition = (flags & modeNoTruncate) ? OPEN_ALWAYS : CREATE_ALWAYS;

    const HANDLE file =
        gameapi::CreateFileA(path, access, share, &security, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (error) {
            error->osError = static_cast<long>(gameapi::GetLastError());
            error->cause = OsErrorToException(error->osError);
            error->fileName = path;
        }
        return FALSE;
    }
    handle = file;
    closeOnDelete = TRUE;
    return TRUE;
}

unsigned CFile::Read(void* buffer, unsigned count) {
    if (count == 0) return 0;
    DWORD read;
    if (!gameapi::ReadFile(handle, buffer, count, &read, nullptr))
        ThrowOsError(static_cast<long>(gameapi::GetLastError()), nullptr);
    return read;
}

void CFile::Write(const void* buffer, unsigned count) {
    if (count == 0) return;
    DWORD written;
    if (!gameapi::WriteFile(handle, buffer, count, &written, nullptr))
        ThrowOsError(static_cast<long>(gameapi::GetLastError()), fileName);
    if (written != count) ThrowFileException(CFileException::diskFull, -1, fileName);
}

long CFile::Seek(long offset, unsigned from) {
    const DWORD position = gameapi::SetFilePointer(handle, offset, nullptr, from);
    if (position == INVALID_SET_FILE_POINTER) ThrowOsError(static_cast<long>(gameapi::GetLastError()), nullptr);
    return static_cast<long>(position);
}

unsigned long CFile::GetPosition() const {
    const DWORD position = gameapi::SetFilePointer(handle, 0, nullptr, FILE_CURRENT);
    if (position == INVALID_SET_FILE_POINTER) ThrowOsError(static_cast<long>(gameapi::GetLastError()), nullptr);
    return position;
}

void CFile::Flush() {
    if (handle == kNoFile) return;
    if (!gameapi::FlushFileBuffers(handle)) ThrowOsError(static_cast<long>(gameapi::GetLastError()), nullptr);
}

void CFile::Close() {
    BOOL failed = FALSE;
    if (handle != kNoFile) failed = !gameapi::CloseHandle(handle);
    handle = kNoFile;
    closeOnDelete = FALSE;
    fileName.Empty();
    if (failed) ThrowOsError(static_cast<long>(gameapi::GetLastError()), nullptr);
}

// The handle is closed without a check; the close-on-delete flag stays.
void CFile::Abort() {
    if (handle != kNoFile) {
        gameapi::CloseHandle(handle);
        handle = kNoFile;
    }
    fileName.Empty();
}

void CFile::LockRange(unsigned long position, unsigned long count) {
    if (!gameapi::LockFile(handle, position, 0, count, 0)) ThrowOsError(static_cast<long>(gameapi::GetLastError()), nullptr);
}

void CFile::UnlockRange(unsigned long position, unsigned long count) {
    if (!gameapi::UnlockFile(handle, position, 0, count, 0)) ThrowOsError(static_cast<long>(gameapi::GetLastError()), nullptr);
}

void CFile::SetLength(unsigned long length) {
    Virtuals(this)->Seek(static_cast<long>(length), FILE_BEGIN);
    if (!gameapi::SetEndOfFile(handle)) ThrowOsError(static_cast<long>(gameapi::GetLastError()), nullptr);
}

unsigned long CFile::GetLength() const {
    auto* self = const_cast<CFile*>(this);
    const long position = Virtuals(self)->Seek(0, FILE_CURRENT);
    const long length = Virtuals(self)->Seek(0, FILE_END);
    Virtuals(self)->Seek(position, FILE_BEGIN);
    return static_cast<unsigned long>(length);
}

// A new CFile on a duplicate of the handle. When the duplicate fails, the new object is deleted and the
// error thrown.
CFile* CFile::Duplicate() const {
    auto* copy = static_cast<CFile*>(gamecrt::operator_new(sizeof(CFile)));
    if (copy) copy->ConstructHandle(kNoFile);
    HANDLE duplicate;
    if (!gameapi::DuplicateHandle(gameapi::GetCurrentProcess(), handle, gameapi::GetCurrentProcess(), &duplicate, 0, FALSE,
                                  DUPLICATE_SAME_ACCESS)) {
        if (copy) Virtuals(copy)->DeletingDestructor(1);
        ThrowOsError(static_cast<long>(gameapi::GetLastError()), nullptr);
    }
    copy->handle = duplicate;
    copy->closeOnDelete = closeOnDelete;
    return copy;
}

void CFile::SetFilePath(const char* path) { fileName = path; }

unsigned CFile::GetBufferPtr(unsigned, unsigned, void**, void**) { return 0; }

// ---- CStdioFile -----------------------------------------------------------------------------------

CStdioFile* CStdioFile::Construct() {
    CFile::Construct();
    stream = nullptr;
    vtable = Vtable();
    return this;
}

CStdioFile* CStdioFile::ConstructOpen(const char* path, unsigned flags) {
    CFile::Construct();
    UnwindFile unwind(this);
    vtable = Vtable();
    CFileException error;
    error.InitEmpty();
    if (!Open(path, flags, &error)) ThrowFileException(error.cause, error.osError, error.fileName);
    error.vtable = CFileException::Vtable();
    unwind.Disarm();
    return this;
}

void CStdioFile::Destruct() {
    vtable = Vtable();
    if (stream && closeOnDelete) Close();
    CFile::Destruct();
}

CStdioFile* CStdioFile::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

BOOL CStdioFile::Open(const char* path, unsigned flags, CFileException* error) {
    stream = nullptr;
    if (!CFile::Open(path, flags & ~typeText, error)) return FALSE;
    char mode[4];
    int length = 0;
    if (flags & modeCreate) mode[length++] = (flags & modeNoTruncate) ? 'a' : 'w';
    else if (flags & modeWrite) mode[length++] = 'a';
    else mode[length++] = 'r';
    if ((mode[0] == 'r' && (flags & modeReadWrite)) || (mode[0] != 'r' && !(flags & modeWrite))) mode[length++] = '+';
    int descriptorFlags = _O_TEXT;
    if (flags & typeBinary) {
        mode[length++] = 'b';
        descriptorFlags = 0;
    } else {
        mode[length++] = 't';
    }
    mode[length] = '\0';
    const int descriptor = OpenOsfhandle(handle, descriptorFlags);
    if (descriptor != -1) stream = Fdopen(descriptor, mode);
    if (!stream) {
        if (error) {
            error->osError = DosErrno();
            error->cause = OsErrorToException(DosErrno());
        }
        CFile::Abort();
        return FALSE;
    }
    return TRUE;
}

unsigned CStdioFile::Read(void* buffer, unsigned count) {
    if (count == 0) return 0;
    const unsigned read = static_cast<unsigned>(gamecrt::fread(buffer, 1, count, stream));
    if (read == 0 && !AtEnd(stream)) ThrowFileException(CFileException::generic, DosErrno(), fileName);
    if (Failed(stream)) {
        Clearerr(stream);
        ThrowFileException(CFileException::generic, DosErrno(), fileName);
    }
    return read;
}

void CStdioFile::Write(const void* buffer, unsigned count) {
    if (gamecrt::fwrite(buffer, 1, count, stream) != count) ThrowFileException(CFileException::generic, DosErrno(), fileName);
}

void CStdioFile::WriteString(const char* text) {
    if (Fputs(text, stream) == EOF) ThrowFileException(CFileException::diskFull, DosErrno(), fileName);
}

char* CStdioFile::ReadString(char* buffer, unsigned size) {
    char* result = Fgets(buffer, static_cast<int>(size), stream);
    if (!result && !AtEnd(stream)) {
        Clearerr(stream);
        ThrowFileException(CFileException::generic, DosErrno(), fileName);
    }
    return result;
}

// A line without its '\n', read 128 characters at a time (fgets is given 129: one more than the
// buffer asked for, as in MFC).
BOOL CStdioFile::ReadLine(CString& text) {
    constexpr int kChunk = 128;
    text = ChNil();
    char* at = text.GetBuffer(kChunk);
    char* result;
    for (;;) {
        result = Fgets(at, kChunk + 1, stream);
        text.ReleaseBuffer(-1);
        if (!result) {
            if (!AtEnd(stream)) {
                Clearerr(stream);
                ThrowFileException(CFileException::generic, DosErrno(), fileName);
            }
            break;
        }
        const int length = gameapi::lstrlenA(at);
        if (length < kChunk || at[length - 1] == '\n') break;
        const int sofar = text.GetLength();
        at = text.GetBuffer(sofar + kChunk) + sofar;
    }
    const char* data = text.GetBuffer(0);
    const int length = text.GetLength();
    if (length != 0 && data[length - 1] == '\n') text.GetBufferSetLength(length - 1);
    return result != nullptr;
}

long CStdioFile::Seek(long offset, unsigned from) {
    if (gamecrt::fseek(stream, offset, static_cast<int>(from)) != 0) ThrowFileException(CFileException::badSeek, DosErrno(), fileName);
    return gamecrt::ftell(stream);
}

unsigned long CStdioFile::GetPosition() const {
    const long position = gamecrt::ftell(stream);
    if (position == -1) ThrowFileException(CFileException::invalidFile, DosErrno(), fileName);
    return static_cast<unsigned long>(position);
}

void CStdioFile::Flush() {
    if (stream && Fflush(stream) != 0) ThrowFileException(CFileException::diskFull, DosErrno(), fileName);
}

void CStdioFile::Close() {
    int failed = 0;
    if (stream) failed = gamecrt::fclose(stream);
    handle = kNoFile;
    closeOnDelete = FALSE;
    stream = nullptr;
    if (failed) ThrowFileException(CFileException::diskFull, DosErrno(), fileName);
}

void CStdioFile::Abort() {
    if (stream && closeOnDelete) gamecrt::fclose(stream);
    handle = kNoFile;
    stream = nullptr;
    closeOnDelete = FALSE;
}

CFile* CStdioFile::Duplicate() const {
    ThrowNotSupported();
    return nullptr;
}

void CStdioFile::LockRange(unsigned long, unsigned long) { ThrowNotSupported(); }

// ---- CFileFind ------------------------------------------------------------------------------------

CFileFind* CFileFind::Construct() {
    new (&root) CString();
    vtable = Vtable();
    found = nullptr;
    next = nullptr;
    context = nullptr;
    separator = '\\';
    return this;
}

void CFileFind::Destruct() {
    vtable = Vtable();
    Close();
    root.~CString();
}

CFileFind* CFileFind::DeletingDestructor(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

void CFileFind::Close() {
    if (found) {
        gamecrt::operator_delete(found);
        found = nullptr;
    }
    if (next) {
        gamecrt::operator_delete(next);
        next = nullptr;
    }
    if (context && context != INVALID_HANDLE_VALUE) {
        Virtuals(this)->CloseContext();
        context = nullptr;
    }
}

BOOL CFileFind::CloseContext() { return gameapi::FindClose(context); }

BOOL CFileFind::FindFile(const char* pattern, unsigned long) {
    Close();
    next = static_cast<WIN32_FIND_DATAA*>(gamecrt::operator_new(sizeof(WIN32_FIND_DATAA)));
    gotLast = FALSE;
    if (!pattern) pattern = AllFiles();
    gameapi::lstrcpyA(next->cFileName, pattern);
    context = gameapi::FindFirstFileA(pattern, next);
    if (context == INVALID_HANDLE_VALUE) {
        const DWORD error = gameapi::GetLastError();
        Close();
        gameapi::SetLastError(error);
        return FALSE;
    }
    char* path = root.GetBufferSetLength(MAX_PATH);
    if (!Fullpath(path, pattern, MAX_PATH)) {
        root.ReleaseBuffer(-1);
        Close();
        gameapi::SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    // The root is everything before the last separator.
    char* back = reinterpret_cast<char*>(libcrt::Mbsrchr(reinterpret_cast<uchar*>(path), '\\'));
    char* front = reinterpret_cast<char*>(libcrt::Mbsrchr(reinterpret_cast<uchar*>(path), '/'));
    if (front || back) {
        if (!front) front = path;
        if (!back) back = path;
        if (front >= back) *front = '\0';
        else *back = '\0';
    }
    root.ReleaseBuffer(-1);
    return TRUE;
}

// The next entry: the one read ahead becomes the found one; FALSE once the search has no more.
BOOL CFileFind::FindNextFile() {
    if (!context) return FALSE;
    if (!found) found = static_cast<WIN32_FIND_DATAA*>(gamecrt::operator_new(sizeof(WIN32_FIND_DATAA)));
    WIN32_FIND_DATAA* swapped = found;
    found = next;
    next = swapped;
    return gameapi::FindNextFileA(context, swapped);
}

BOOL CFileFind::MatchesMask(unsigned long mask) const { return found ? ((found->dwFileAttributes & mask) != 0) : FALSE; }

BOOL CFileFind::IsDots() const {
    if (!found || !Virtuals(this)->MatchesMask(FILE_ATTRIBUTE_DIRECTORY)) return FALSE;
    const char* name = found->cFileName;
    return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

unsigned long CFileFind::GetLength() const { return found ? found->nFileSizeLow : 0; }

BOOL CFileFind::GetLastWriteFileTime(FILETIME* time) const {
    if (!found || !time) return FALSE;
    *time = found->ftLastWriteTime;
    return TRUE;
}

BOOL CFileFind::GetLastAccessFileTime(FILETIME* time) const {
    if (!found || !time) return FALSE;
    *time = found->ftLastAccessTime;
    return TRUE;
}

BOOL CFileFind::GetCreationFileTime(FILETIME* time) const {
    if (!found || !time) return FALSE;
    *time = found->ftCreationTime;
    return TRUE;
}

BOOL CFileFind::GetLastWriteTime(CTime* time) const {
    if (!found) return FALSE;
    CTime converted;
    time->time = converted.ConstructFileTime(found->ftLastWriteTime, -1)->time;
    return TRUE;
}

BOOL CFileFind::GetLastAccessTime(CTime* time) const {
    if (!found) return FALSE;
    CTime converted;
    time->time = converted.ConstructFileTime(found->ftLastAccessTime, -1)->time;
    return TRUE;
}

BOOL CFileFind::GetCreationTime(CTime* time) const {
    if (!found) return FALSE;
    CTime converted;
    time->time = converted.ConstructFileTime(found->ftCreationTime, -1)->time;
    return TRUE;
}

CString CFileFind::GetFileName() const {
    CString name;
    if (found) name = found->cFileName;
    return name;
}

// The root, a separator unless it ends in one, and the name.
CString CFileFind::GetFilePath() const {
    CString path(root);
    const char last = static_cast<const char*>(path)[path.GetLength() - 1];
    if (last != '\\' && last != '/') path += separator;
    path += Virtuals(this)->GetFileName();
    return path;
}

// The name without its extension (_splitpath of the full path).
CString CFileFind::GetFileTitle() const {
    const CString path = Virtuals(this)->GetFilePath();
    CString title;
    gamecrt::splitpath(path, nullptr, nullptr, title.GetBuffer(MAX_PATH), nullptr);
    title.ReleaseBuffer(-1);
    return title;
}

CString CFileFind::GetFileURL() const {
    CString url(FileUrlPrefix());
    url += Virtuals(this)->GetFilePath();
    return url;
}

CString CFileFind::GetRoot() const { return CString(root); }

EI_REPLACE(0x00701870, FullPath);
EI_REPLACE(0x00701940, GetRoot);
EI_REPLACE(0x0070BE58, FileRuntimeClass);
EI_REPLACE(0x0070C15B, StdioFileRuntimeClass);
EI_REPLACE(0x0070BE5E, FileFindRuntimeClass);

EI_REPLACE_METHOD(0x00701036, CFile::Construct);
EI_REPLACE_METHOD(0x0070106C, CFile::ConstructHandle);
EI_REPLACE_METHOD(0x0070108B, CFile::ConstructOpen);
EI_REPLACE_METHOD(0x0070112B, CFile::Destruct);
EI_REPLACE_METHOD(0x00701050, CFile::DeletingDestructor);
EI_REPLACE_METHOD(0x007013B9, CFile::GetPosition);
EI_REPLACE_METHOD(0x006E6361, CFile::SetFilePath);
EI_REPLACE_METHOD(0x007011E8, CFile::Open);
EI_REPLACE_METHOD(0x00701173, CFile::Duplicate);
EI_REPLACE_METHOD(0x0070138A, CFile::Seek);
EI_REPLACE_METHOD(0x007014B6, CFile::SetLength);
EI_REPLACE_METHOD(0x007014E2, CFile::GetLength);
EI_REPLACE_METHOD(0x00701305, CFile::Read);
EI_REPLACE_METHOD(0x0070133F, CFile::Write);
EI_REPLACE_METHOD(0x00701464, CFile::LockRange);
EI_REPLACE_METHOD(0x0070148D, CFile::UnlockRange);
EI_REPLACE_METHOD(0x00701444, CFile::Abort);
EI_REPLACE_METHOD(0x007013E2, CFile::Flush);
EI_REPLACE_METHOD(0x00701403, CFile::Close);
EI_REPLACE_METHOD(0x0070150F, CFile::GetBufferPtr);

EI_REPLACE_METHOD(0x00701B79, CStdioFile::Construct);
EI_REPLACE_METHOD(0x00701BD4, CStdioFile::ConstructOpen);
EI_REPLACE_METHOD(0x00701C6D, CStdioFile::Destruct);
EI_REPLACE_METHOD(0x00701B8F, CStdioFile::DeletingDestructor);
EI_REPLACE_METHOD(0x00701F8E, CStdioFile::GetPosition);
EI_REPLACE_METHOD(0x00701CB0, CStdioFile::Open);
EI_REPLACE_METHOD(0x0070203B, CStdioFile::Duplicate);
EI_REPLACE_METHOD(0x00701F56, CStdioFile::Seek);
EI_REPLACE_METHOD(0x00701D84, CStdioFile::Read);
EI_REPLACE_METHOD(0x00701DF0, CStdioFile::Write);
EI_REPLACE_METHOD(0x00702043, CStdioFile::LockRange);
EI_REPLACE_METHOD(0x0070204B, CStdioFile::LockRange);   // UnlockRange: the same body
EI_REPLACE_METHOD(0x00702016, CStdioFile::Abort);
EI_REPLACE_METHOD(0x00701FB8, CStdioFile::Flush);
EI_REPLACE_METHOD(0x00701FE0, CStdioFile::Close);
EI_REPLACE_METHOD(0x00701E23, CStdioFile::WriteString);
EI_REPLACE_METHOD(0x00701E93, CStdioFile::ReadLine);
EI_REPLACE_METHOD(0x00701E4E, CStdioFile::ReadString);

EI_REPLACE_METHOD(0x006E6370, CFileFind::Construct);
EI_REPLACE_METHOD(0x006E63AD, CFileFind::Destruct);
EI_REPLACE_METHOD(0x006E6391, CFileFind::DeletingDestructor);
EI_REPLACE_METHOD(0x006E63E9, CFileFind::Close);
EI_REPLACE_METHOD(0x006E6429, CFileFind::CloseContext);
EI_REPLACE_METHOD(0x006E6433, CFileFind::FindFile);
EI_REPLACE_METHOD(0x006E6653, CFileFind::FindNextFile);
EI_REPLACE_METHOD(0x006E6513, CFileFind::MatchesMask);
EI_REPLACE_METHOD(0x006E661A, CFileFind::IsDots);
EI_REPLACE_METHOD(0x006E68AA, CFileFind::GetLength);
EI_REPLACE_METHOD(0x006E684B, CFileFind::GetFileName);
EI_REPLACE_METHOD(0x006E6722, CFileFind::GetFilePath);
EI_REPLACE_METHOD(0x006E67BA, CFileFind::GetFileTitle);
EI_REPLACE_METHOD(0x006E668E, CFileFind::GetFileURL);
EI_REPLACE_METHOD(0x006E6707, CFileFind::GetRoot);
EI_REPLACE_METHOD(0x006E65C4, CFileFind::GetLastWriteTime);
EI_REPLACE_METHOD(0x006E6551, CFileFind::GetLastWriteFileTime);
EI_REPLACE_METHOD(0x006E6599, CFileFind::GetLastAccessTime);
EI_REPLACE_METHOD(0x006E652D, CFileFind::GetLastAccessFileTime);
EI_REPLACE_METHOD(0x006E65EF, CFileFind::GetCreationTime);
EI_REPLACE_METHOD(0x006E6575, CFileFind::GetCreationFileTime);

} // namespace mfc::impl
