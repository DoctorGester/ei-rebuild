// MFC 4.2's CFile, CStdioFile and CFileFind (mfc_file.h).
#include "compiled_vtable.h"
#include "crt.h"
#include "mfc_file.h"
#include "mfc_runtime_class.h"

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
#include <new>

namespace mfc::impl {

// CFileFind's vtable slots that its own members call (mfc::CFileFindVirtuals with this file's
// CString); declared only.
class __declspec(novtable) FindSlots {
public:
    virtual void GetRuntimeClass();
    virtual void DeletingDestructor();
    virtual void Serialize();
    virtual CString GetFileName() const;
    virtual CString GetFilePath() const;
    virtual CString GetFileTitle() const;
    virtual CString GetFileURL() const;
    virtual CString GetRoot() const;
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

// _doserrno: the Win32 error of the host CRT's last failed call.
long DosErrno() { return static_cast<long>(_doserrno); }
// _fullpath into a caller's buffer: GetFullPathNameA, null when it fails or does not fit (an empty
// path is the current directory, through the image's GetCurrentDirectoryA, in UTF-8).
char* Fullpath(char* out, const char* path, std::size_t size) {
    if (!path || !*path) {
        const DWORD length = gameapi::GetCurrentDirectoryA(static_cast<DWORD>(size), out);
        return length != 0 && length < size ? out : nullptr;
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

// While armed, destroys a string when the frame unwinds: a constructor that throws leaves the
// members it had built destroyed.
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

void GetRoot(const char* path, CString& root) {
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

BOOL FullPath(char* out, const char* path) {
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
    virtual CString GetFileName() const { return This()->GetFileName(); }
    virtual CString GetFileTitle() const { return This()->GetFileTitle(); }
    virtual CString GetFilePath() const { return This()->GetFilePath(); }
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
    virtual CString GetFileName() const { return This()->GetFileName(); }
    virtual CString GetFilePath() const { return This()->GetFilePath(); }
    virtual CString GetFileTitle() const { return This()->GetFileTitle(); }
    virtual CString GetFileURL() const { return This()->GetFileURL(); }
    virtual CString GetRoot() const { return This()->GetRoot(); }
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
    if (!Open(path, flags, &error)) ThrowFileException(error.cause, error.osError, error.fileName);
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

BOOL CFile::GetStatus(CFileStatus& status) const {
    std::memset(&status, 0, sizeof status);
    gameapi::lstrcpynA(status.fullName, fileName, MAX_PATH);
    if (handle == kNoFile) return TRUE;
    FILETIME created, accessed, modified;
    if (!gameapi::GetFileTime(handle, &created, &accessed, &modified)) return FALSE;
    status.size = static_cast<long>(gameapi::GetFileSize(handle, nullptr));
    if (status.size == -1) return FALSE;
    if (fileName.IsEmpty()) {
        status.attribute = 0;
    } else {
        const DWORD attributes = gameapi::GetFileAttributesA(fileName);
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

CString CFile::GetFileName() const {
    CFileStatus status;
    GetStatus(status);
    CString name;
    AfxGetFileName(status.fullName, name.GetBuffer(kMaxFileName), kMaxFileName);
    name.ReleaseBuffer(-1);
    return name;
}

CString CFile::GetFileTitle() const {
    CFileStatus status;
    GetStatus(status);
    CString title;
    AfxGetFileTitle(status.fullName, title.GetBuffer(kMaxFileName), kMaxFileName);
    title.ReleaseBuffer(-1);
    return title;
}

CString CFile::GetFilePath() const {
    CFileStatus status;
    GetStatus(status);
    return CString(status.fullName);
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
    auto* copy = gamecrt::operator_new_as<CFile>();
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
    if (!Open(path, flags, &error)) ThrowFileException(error.cause, error.osError, error.fileName);
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
    const int descriptor = _open_osfhandle(reinterpret_cast<intptr_t>(handle), descriptorFlags);
    if (descriptor != -1) stream = _fdopen(descriptor, mode);
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
    const unsigned read = static_cast<unsigned>(std::fread(buffer, 1, count, stream));
    if (read == 0 && !std::feof(stream)) ThrowFileException(CFileException::generic, DosErrno(), fileName);
    if (std::ferror(stream)) {
        std::clearerr(stream);
        ThrowFileException(CFileException::generic, DosErrno(), fileName);
    }
    return read;
}

void CStdioFile::Write(const void* buffer, unsigned count) {
    if (std::fwrite(buffer, 1, count, stream) != count) ThrowFileException(CFileException::generic, DosErrno(), fileName);
}

void CStdioFile::WriteString(const char* text) {
    if (std::fputs(text, stream) == EOF) ThrowFileException(CFileException::diskFull, DosErrno(), fileName);
}

char* CStdioFile::ReadString(char* buffer, unsigned size) {
    char* result = std::fgets(buffer, static_cast<int>(size), stream);
    if (!result && !std::feof(stream)) {
        std::clearerr(stream);
        ThrowFileException(CFileException::generic, DosErrno(), fileName);
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
        result = std::fgets(chunk, kChunk + 1, stream);
        if (!result) {
            if (!std::feof(stream)) {
                std::clearerr(stream);
                ThrowFileException(CFileException::generic, DosErrno(), fileName);
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
    if (std::fseek(stream, offset, static_cast<int>(from)) != 0) ThrowFileException(CFileException::badSeek, DosErrno(), fileName);
    return std::ftell(stream);
}

unsigned long CStdioFile::GetPosition() const {
    const long position = std::ftell(stream);
    if (position == -1) ThrowFileException(CFileException::invalidFile, DosErrno(), fileName);
    return static_cast<unsigned long>(position);
}

void CStdioFile::Flush() {
    if (stream && std::fflush(stream) != 0) ThrowFileException(CFileException::diskFull, DosErrno(), fileName);
}

void CStdioFile::Close() {
    int failed = 0;
    if (stream) failed = std::fclose(stream);
    handle = kNoFile;
    closeOnDelete = FALSE;
    stream = nullptr;
    if (failed) ThrowFileException(CFileException::diskFull, DosErrno(), fileName);
}

void CStdioFile::Abort() {
    if (stream && closeOnDelete) std::fclose(stream);
    handle = kNoFile;
    stream = nullptr;
    closeOnDelete = FALSE;
}

CFile* CStdioFile::Duplicate() const {
    ThrowNotSupported();
    return nullptr;
}

void CStdioFile::LockRange(unsigned long, unsigned long) { ThrowNotSupported(); }


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
    char* path = root.GetBufferSetLength(MAX_PATH);
    if (!Fullpath(path, pattern, MAX_PATH)) {
        root.ReleaseBuffer(-1);
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
    root.ReleaseBuffer(-1);
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
#pragma warning(suppress : 4996)
    _splitpath(path, nullptr, nullptr, title.GetBuffer(MAX_PATH), nullptr);
    title.ReleaseBuffer(-1);
    return title;
}

CString CFileFind::GetFileURL() const {
    CString url(FileUrlPrefix());
    url += Virtuals(this)->GetFilePath();
    return url;
}

CString CFileFind::GetRoot() const { return CString(root); }

} // namespace mfc::impl
