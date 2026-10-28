// MFC 4.2's CFile, CStdioFile and CFileFind (mfc_file.h).
#include "compiled_vtable.h"
#include "crt.h"
#include "mfc_file.h"
#include "mfc_runtime_class.h"
#include "str.h"

#include "gameapi.h"
#include "game/game_data.h"
#include "game/asset_text.h"

#include <commdlg.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#include <mbstring.h>
#include <memory>
#include <new>
#include <string>

namespace mfc::impl {

// CFileFind's vtable slots that its own members call; declared only.
class __declspec(novtable) FindSlots {
public:
    virtual void GetRuntimeClass();
    virtual void DeletingDestructor();
    virtual void Serialize();
    virtual std::string GetFileName() const;
    virtual std::string GetFilePath() const;
    virtual std::string GetFileTitle() const;
    virtual std::string GetFileURL() const;
    virtual std::string GetRoot() const;
    virtual BOOL GetLastWriteTime(CTime* time) const;
    virtual BOOL GetLastWriteFileTime(FILETIME* time) const;
    virtual BOOL GetLastAccessTime(CTime* time) const;
    virtual BOOL GetLastAccessFileTime(FILETIME* time) const;
    virtual BOOL GetCreationTime(CTime* time) const;
    virtual BOOL GetCreationFileTime(FILETIME* time) const;
    virtual BOOL MatchesMask(unsigned long mask) const;
    virtual BOOL IsDots() const;
    virtual BOOL FindFile(const char* pattern, unsigned long flags);
    virtual BOOL FindNextFile();
    virtual BOOL CloseContext();
};

namespace {

using uchar = unsigned char;

mfc::CFileVirtuals* Virtuals(CFile* file) { return reinterpret_cast<mfc::CFileVirtuals*>(file); }
const mfc::CFileVirtuals* Virtuals(const CFile* file) { return reinterpret_cast<const mfc::CFileVirtuals*>(file); }
const FindSlots* Virtuals(const CFileFind* find) { return reinterpret_cast<const FindSlots*>(find); }
FindSlots* Virtuals(CFileFind* find) { return reinterpret_cast<FindSlots*>(find); }

// "*.*", FindFile's pattern when given none; "file://", GetFileURL's prefix.
const char* AllFiles() { return "*.*"; }
const char* FileUrlPrefix() { return "file://"; }
// afxChNil: the empty text ReadString starts a line from.
const char g_chNil[1] = /*007C8BA8*/ {""};
const char* ChNil() { return g_chNil; }

// ---- The CRT stream calls of CStdioFile -----------------------------------------------------------
// The game's FILE* is the host CRT's (crt_file.cpp): these are the host's.

FILE* Host(gamecrt::CrtFile* stream) { return reinterpret_cast<FILE*>(stream); }

int OpenOsfhandle(HANDLE file, int flags) { return _open_osfhandle(reinterpret_cast<intptr_t>(file), flags); }
gamecrt::CrtFile* Fdopen(int fd, const char* mode) { return reinterpret_cast<gamecrt::CrtFile*>(_fdopen(fd, mode)); }
long DosErrno() { return static_cast<long>(_doserrno); }
int Fputs(const char* text, gamecrt::CrtFile* stream) { return std::fputs(text, Host(stream)); }
char* Fgets(char* buffer, int size, gamecrt::CrtFile* stream) { return std::fgets(buffer, size, Host(stream)); }
void Clearerr(gamecrt::CrtFile* stream) { std::clearerr(Host(stream)); }
int Fflush(gamecrt::CrtFile* stream) { return std::fflush(Host(stream)); }
bool AtEnd(gamecrt::CrtFile* stream) { return std::feof(Host(stream)) != 0; }
bool Failed(gamecrt::CrtFile* stream) { return std::ferror(Host(stream)) != 0; }
// _fullpath into a caller's buffer: GetFullPathNameA, null when it fails or does not fit (an empty
// path is the current directory; EI_UTF8: through the image's GetCurrentDirectoryA, in UTF-8).
char* Fullpath(char* out, const char* path, std::size_t size) {
    if (!path || !*path) {
        if constexpr (text::kUtf8) {
            const DWORD length = gameapi::GetCurrentDirectoryA(static_cast<DWORD>(size), out);
            return length != 0 && length < size ? out : nullptr;
        } else {
            return _getcwd(out, static_cast<int>(size));
        }
    }
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
char* Next(char* p) { return const_cast<char*>(gamecrt::mbsinc(p)); }
const char* Next(const char* p) { return gamecrt::mbsinc(p); }


constexpr unsigned kMaxFileName = 256;

// AfxGetRoot: the root of `path` ("x:\" or "\\server\share\").
std::string GetRoot(const char* path) {
    std::string root(MAX_PATH, '\0');
    char* start = root.data();
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
    str::CutAtNull(root);
    return root;
}

} // namespace

// ---- Paths ----------------------------------------------------------------------------------------

BOOL FullPath(char* out, const char* path) {
    char* filePart;
    if (!gameapi::GetFullPathNameA(path, MAX_PATH, out, &filePart)) {
        gameapi::lstrcpynA(out, path, MAX_PATH);
        return FALSE;
    }
    const std::string root = GetRoot(out);
    DWORD unused, flags;
    if (!gameapi::GetVolumeInformationA(root.c_str(), nullptr, 0, nullptr, &unused, &flags, nullptr, 0)) return FALSE;
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

unsigned AfxGetFileName(const char* path, char* name, unsigned max) {
    const char* last = path;
    for (const char* at = path; *at != '\0'; at = Next(at)) {
        if (*at == '\\' || *at == '/' || *at == ':') last = Next(at);
    }
    if (!name) return static_cast<unsigned>(gameapi::lstrlenA(last) + 1);
    gameapi::lstrcpynA(name, last, static_cast<int>(max));
    return 0;
}

unsigned AfxGetFileTitle(const char* path, char* title, unsigned max) {
    char scratch[MAX_PATH];
    char* into = title;
    if (!into) {
        into = scratch;
        max = MAX_PATH;
    }
    if (::GetFileTitleA(path, into, static_cast<WORD>(max)) != 0) return AfxGetFileName(path, title, max);
    return title ? 0 : static_cast<unsigned>(gameapi::lstrlenA(into) + 1);
}

// ---- The compiled tables --------------------------------------------------------------------

namespace {

// CFile's 21 slots (MFC 4.2's CFile virtuals), each the body of `File` that game.exe's table points at: CFile's
// for, CStdioFile's own (or the CFile one it inherits) for.
template <typename Self, typename File, const CRuntimeClass* kClass>
struct FileTable : ei::CompiledTag<Self> {
    virtual const CRuntimeClass* GetRuntimeClass() const { return kClass; }
    virtual void* DeleteScalar(unsigned flags) { return This()->DeletingDestructor(flags); }
    virtual void Serialize(void*) {}
    virtual unsigned long GetPosition() const { return This()->GetPosition(); }
    virtual std::string GetFileName() const { return This()->GetFileName(); }
    virtual std::string GetFileTitle() const { return This()->GetFileTitle(); }
    virtual std::string GetFilePath() const { return This()->GetFilePath(); }
    virtual void SetFilePath(const char* path) { This()->SetFilePath(path); }
    virtual BOOL Open(const char* path, unsigned flags, CFileException* error) {
        return This()->Open(path, flags, error);
    }
    virtual CFile* Duplicate() const { return This()->Duplicate(); }
    virtual long Seek(long offset, unsigned from) { return This()->Seek(offset, from); }
    virtual void SetLength(unsigned long length) { This()->SetLength(length); }
    virtual unsigned long GetLength() const { return This()->GetLength(); }
    virtual unsigned Read(void* buffer, unsigned count) { return This()->Read(buffer, count); }
    virtual void Write(const void* buffer, unsigned count) { This()->Write(buffer, count); }
    virtual void LockRange(unsigned long position, unsigned long count) { This()->LockRange(position, count); }
    virtual void UnlockRange(unsigned long position, unsigned long count) { This()->UnlockRange(position, count); }
    virtual void Abort() { This()->Abort(); }
    virtual void Flush() { This()->Flush(); }
    virtual void Close() { This()->Close(); }
    virtual unsigned GetBufferPtr(unsigned command, unsigned count, void** start, void** end) {
        return This()->GetBufferPtr(command, count, start, end);
    }

protected:
    File* This() { return this->template ObjectAs<File>(); }
    const File* This() const { return this->template ObjectAs<File>(); }
};

// The plain CFile's table.
struct PlainFileTable final : FileTable<PlainFileTable, CFile, &kClassCFile> {};

// CStdioFile's slots 3, 8-10, 13-19, the CFile ones at 4-7 / 11 /
// 12 / 20, and 21-23 its own. UnlockRange is LockRange's body (not supported).
struct StdioFileTable final : FileTable<StdioFileTable, CStdioFile, &kClassCStdioFile> {
    void UnlockRange(unsigned long position, unsigned long count) override { This()->LockRange(position, count); }
    virtual void WriteString(const char* text) { This()->WriteString(text); }
    virtual BOOL ReadLine(std::string& text) { return This()->ReadLine(text); }
    virtual char* ReadString(char* buffer, unsigned size) { return This()->ReadString(buffer, size); }
};

// CFileFind's 19 slots (game.exe's table).
struct FileFindTable final : ei::CompiledTag<FileFindTable> {
    virtual const CRuntimeClass* GetRuntimeClass() const { return &kClassCFileFind; }
    virtual void* DeleteScalar(unsigned flags) { return This()->DeletingDestructor(flags); }
    virtual void Serialize(void*) {}
    virtual std::string GetFileName() const { return This()->GetFileName(); }
    virtual std::string GetFilePath() const { return This()->GetFilePath(); }
    virtual std::string GetFileTitle() const { return This()->GetFileTitle(); }
    virtual std::string GetFileURL() const { return This()->GetFileURL(); }
    virtual std::string GetRoot() const { return This()->GetRoot(); }
    virtual BOOL GetLastWriteTime(CTime* time) const { return This()->GetLastWriteTime(time); }
    virtual BOOL GetLastWriteFileTime(FILETIME* time) const { return This()->GetLastWriteFileTime(time); }
    virtual BOOL GetLastAccessTime(CTime* time) const { return This()->GetLastAccessTime(time); }
    virtual BOOL GetLastAccessFileTime(FILETIME* time) const { return This()->GetLastAccessFileTime(time); }
    virtual BOOL GetCreationTime(CTime* time) const { return This()->GetCreationTime(time); }
    virtual BOOL GetCreationFileTime(FILETIME* time) const { return This()->GetCreationFileTime(time); }
    virtual BOOL MatchesMask(unsigned long mask) const { return This()->MatchesMask(mask); }
    virtual BOOL IsDots() const { return This()->IsDots(); }
    virtual BOOL FindFile(const char* pattern, unsigned long flags) { return This()->FindFile(pattern, flags); }
    virtual BOOL FindNextFile() { return This()->FindNextFile(); }
    virtual BOOL CloseContext() { return This()->CloseContext(); }

private:
    CFileFind* This() { return ObjectAs<CFileFind>(); }
    const CFileFind* This() const { return ObjectAs<CFileFind>(); }
};

} // namespace

void* CFile::Vtable() { return PlainFileTable::Vtable(); }
void* CStdioFile::Vtable() { return StdioFileTable::Vtable(); }
void* CFileFind::Vtable() { return FileFindTable::Vtable(); }


CFile::CFile() : vtable(Vtable()) {}

CFile::CFile(HANDLE file) : vtable(Vtable()), handle(file) {}

CFile::CFile(const char* path, unsigned flags) : vtable(Vtable()) {
    CFileException error;
    if (!Open(path, flags, &error)) ThrowFileException(error.cause, error.osError, error.fileName.c_str());
}

CFile::~CFile() {
    vtable = Vtable();
    if (handle != kNoFile && closeOnDelete) Close();
}

CFile* CFile::DeletingDestructor(unsigned flags) {
    std::destroy_at(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

BOOL CFile::Open(const char* path, unsigned flags, CFileException* error) {
    closeOnDelete = FALSE;
    handle = kNoFile;
    fileName.clear();
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
        ThrowOsError(static_cast<long>(gameapi::GetLastError()), fileName.c_str());
    if (written != count) ThrowFileException(CFileException::diskFull, -1, fileName.c_str());
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

BOOL CFile::GetStatus(CFileStatus& status) const {
    std::memset(&status, 0, sizeof status);
    gameapi::lstrcpynA(status.fullName, fileName.c_str(), MAX_PATH);
    if (handle == kNoFile) return TRUE;
    FILETIME created, accessed, modified;
    if (!gameapi::GetFileTime(handle, &created, &accessed, &modified)) return FALSE;
    status.size = static_cast<long>(gameapi::GetFileSize(handle, nullptr));
    if (status.size == -1) return FALSE;
    if (fileName.empty()) {
        status.attribute = 0;
    } else {
        const DWORD attributes = gameapi::GetFileAttributesA(fileName.c_str());
        status.attribute = attributes == INVALID_FILE_ATTRIBUTES ? 0 : static_cast<BYTE>(attributes);
    }
    CTime converted;
    status.created.time = converted.ConstructFileTime(created, -1)->time;
    status.accessed.time = converted.ConstructFileTime(accessed, -1)->time;
    status.modified.time = converted.ConstructFileTime(modified, -1)->time;
    if (status.created.time == 0) status.created = status.modified;
    if (status.accessed.time == 0) status.accessed = status.modified;
    return TRUE;
}

std::string CFile::GetFileName() const {
    CFileStatus status;
    GetStatus(status);
    char name[kMaxFileName] = {};
    AfxGetFileName(status.fullName, name, kMaxFileName);
    return name;
}

std::string CFile::GetFileTitle() const {
    CFileStatus status;
    GetStatus(status);
    char title[kMaxFileName] = {};
    AfxGetFileTitle(status.fullName, title, kMaxFileName);
    return title;
}

std::string CFile::GetFilePath() const {
    CFileStatus status;
    GetStatus(status);
    return status.fullName;
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
    fileName.clear();
    if (failed) ThrowOsError(static_cast<long>(gameapi::GetLastError()), nullptr);
}

// The handle is closed without a check; the close-on-delete flag stays.
void CFile::Abort() {
    if (handle != kNoFile) {
        gameapi::CloseHandle(handle);
        handle = kNoFile;
    }
    fileName.clear();
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
    auto* copy = gamecrt::operator_new_as<CFile>();
    if (copy) std::construct_at(copy, kNoFile);
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


CStdioFile::CStdioFile() { vtable = Vtable(); }

// A failed open destroys the CFile part (its destructor) before the exception leaves.
CStdioFile::CStdioFile(const char* path, unsigned flags) {
    vtable = Vtable();
    CFileException error;
    if (!Open(path, flags, &error)) ThrowFileException(error.cause, error.osError, error.fileName.c_str());
}

CStdioFile::~CStdioFile() {
    vtable = Vtable();
    if (stream && closeOnDelete) Close();
}

CStdioFile* CStdioFile::DeletingDestructor(unsigned flags) {
    std::destroy_at(this);
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
    const unsigned read = static_cast<unsigned>(libcrt::Fread(buffer, 1, count, stream));
    if (read == 0 && !AtEnd(stream)) ThrowFileException(CFileException::generic, DosErrno(), fileName.c_str());
    if (Failed(stream)) {
        Clearerr(stream);
        ThrowFileException(CFileException::generic, DosErrno(), fileName.c_str());
    }
    return read;
}

void CStdioFile::Write(const void* buffer, unsigned count) {
    if (gamecrt::fwrite(buffer, 1, count, stream) != count) ThrowFileException(CFileException::generic, DosErrno(), fileName.c_str());
}

void CStdioFile::WriteString(const char* text) {
    if (Fputs(text, stream) == EOF) ThrowFileException(CFileException::diskFull, DosErrno(), fileName.c_str());
}

char* CStdioFile::ReadString(char* buffer, unsigned size) {
    char* result = Fgets(buffer, static_cast<int>(size), stream);
    if (!result && !AtEnd(stream)) {
        Clearerr(stream);
        ThrowFileException(CFileException::generic, DosErrno(), fileName.c_str());
    }
    return result;
}

// A line without its '\n', read 128 characters at a time (fgets is given 129: one more than the
// chunk, as in MFC, which reads into the CString's buffer). A chunk counts up to its first zero byte, as
// ReleaseBuffer(-1) counted it. FALSE when the last fgets read nothing, even after a full chunk.
BOOL CStdioFile::ReadLine(std::string& text) {
    constexpr int kChunk = 128;
    char chunk[kChunk + 1];
    text.clear();
    char* result;
    for (;;) {
        result = Fgets(chunk, kChunk + 1, stream);
        if (!result) {
            if (!AtEnd(stream)) {
                Clearerr(stream);
                ThrowFileException(CFileException::generic, DosErrno(), fileName.c_str());
            }
            break;
        }
        const int length = gameapi::lstrlenA(chunk);
        text.append(chunk, static_cast<std::size_t>(length));
        if (length < kChunk || chunk[length - 1] == '\n') break;
    }
    if (!text.empty() && text.back() == '\n') text.pop_back();
    return result != nullptr;
}

long CStdioFile::Seek(long offset, unsigned from) {
    if (libcrt::Fseek(stream, offset, static_cast<int>(from)) != 0) ThrowFileException(CFileException::badSeek, DosErrno(), fileName.c_str());
    return libcrt::Ftell(stream);
}

unsigned long CStdioFile::GetPosition() const {
    const long position = libcrt::Ftell(stream);
    if (position == -1) ThrowFileException(CFileException::invalidFile, DosErrno(), fileName.c_str());
    return static_cast<unsigned long>(position);
}

void CStdioFile::Flush() {
    if (stream && Fflush(stream) != 0) ThrowFileException(CFileException::diskFull, DosErrno(), fileName.c_str());
}

void CStdioFile::Close() {
    int failed = 0;
    if (stream) failed = libcrt::Fclose(stream);
    handle = kNoFile;
    closeOnDelete = FALSE;
    stream = nullptr;
    if (failed) ThrowFileException(CFileException::diskFull, DosErrno(), fileName.c_str());
}

void CStdioFile::Abort() {
    if (stream && closeOnDelete) libcrt::Fclose(stream);
    handle = kNoFile;
    stream = nullptr;
    closeOnDelete = FALSE;
}

CFile* CStdioFile::Duplicate() const {
    ThrowNotSupported();
    return nullptr;
}

void CStdioFile::LockRange(unsigned long, unsigned long) { ThrowNotSupported(); }


CFileFind::CFileFind() : vtable(Vtable()) {}

CFileFind::~CFileFind() {
    vtable = Vtable();
    Close();
}

CFileFind* CFileFind::DeletingDestructor(unsigned flags) {
    std::destroy_at(this);
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
    next = gamecrt::operator_new_as<WIN32_FIND_DATAA>();
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
    char* path = str::SetLength(root, MAX_PATH);
    if (!Fullpath(path, pattern, MAX_PATH)) {
        str::CutAtNull(root);
        Close();
        gameapi::SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    // The root is everything before the last separator.
    char* back = reinterpret_cast<char*>(_mbsrchr(reinterpret_cast<uchar*>(path), '\\'));
    char* front = reinterpret_cast<char*>(_mbsrchr(reinterpret_cast<uchar*>(path), '/'));
    if (front || back) {
        if (!front) front = path;
        if (!back) back = path;
        if (front >= back) *front = '\0';
        else *back = '\0';
    }
    str::CutAtNull(root);
    return TRUE;
}

// The next entry: the one read ahead becomes the found one; FALSE once the search has no more.
BOOL CFileFind::FindNextFile() {
    if (!context) return FALSE;
    if (!found) found = gamecrt::operator_new_as<WIN32_FIND_DATAA>();
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

std::string CFileFind::GetFileName() const { return found ? found->cFileName : ""; }

// The root, a separator unless it ends in one, and the name.
std::string CFileFind::GetFilePath() const {
    std::string path = root;
    const char last = path.empty() ? '\0' : path.back();
    if (last != '\\' && last != '/') path += separator;
    path += Virtuals(this)->GetFileName();
    return path;
}

// The name without its extension (_splitpath of the full path).
std::string CFileFind::GetFileTitle() const {
    const std::string path = Virtuals(this)->GetFilePath();
    char title[MAX_PATH] = {};
#pragma warning(suppress : 4996)
    _splitpath(path.c_str(), nullptr, nullptr, title, nullptr);
    return title;
}

std::string CFileFind::GetFileURL() const { return FileUrlPrefix() + Virtuals(this)->GetFilePath(); }

std::string CFileFind::GetRoot() const { return root; }

} // namespace mfc::impl
