// The game CRT's file functions in the standalone image (S4b, docs/notes/standalone.md): VC6's _sopen
// and _openfile over the game's import slots, the host CRT's descriptors and streams underneath (crt.h).
#include "crt.h"

#include "ei/hook.h"
#include "gameapi.h"
#include "game/game_data.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>

namespace libcrt {

namespace {

// The game CRT's settings in the exe's data: _fmode (the translation when neither _O_TEXT nor _O_BINARY
// is asked for), _umaskval (permission bits a new file does not get) and _commode (fopen's commit flag).
int GameFileMode() { return ei::GameData<const int>(0x007CAC80); }
int GameUmask() { return ei::GameData<const int>(0x007CAADC); }
int GameCommitMode() { return ei::GameData<const int>(0x007CADA4); }
constexpr int kStreamCommit = 0x4000;   // _IOCOMMIT

// &_iob[0..2]: the game's stdin, stdout and stderr (0x20-byte FILEs).
const char* GameStreams() { return ei::GamePointer<const char>(0x00798060); }
constexpr std::ptrdiff_t kGameStreamSize = 0x20;

// The host stream behind a game stream pointer.
FILE* HostFile(const gamecrt::CrtFile* file) {
    const auto at = reinterpret_cast<const char*>(file);
    if (at == GameStreams()) return stdin;
    if (at == GameStreams() + kGameStreamSize) return stdout;
    if (at == GameStreams() + 2 * kGameStreamSize) return stderr;
    return reinterpret_cast<FILE*>(const_cast<gamecrt::CrtFile*>(file));
}

// While it lives, the host CRT answers an invalid argument (a bad descriptor or stream) with its error
// return, as VC6's release CRT does, instead of ending the process.
class QuietInvalidParameters {
public:
    QuietInvalidParameters() : previous_(_set_thread_local_invalid_parameter_handler(&Ignore)) {}
    ~QuietInvalidParameters() { _set_thread_local_invalid_parameter_handler(previous_); }
    QuietInvalidParameters(const QuietInvalidParameters&) = delete;
    QuietInvalidParameters& operator=(const QuietInvalidParameters&) = delete;

private:
    static void __cdecl Ignore(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, std::uintptr_t) {}
    _invalid_parameter_handler previous_;
};

// A text file opened for update loses a Ctrl-Z that is its last byte (VC6's _sopen); false when a seek
// or the truncation fails.
bool DropTrailingCtrlZ(int fd) {
    constexpr char kCtrlZ = 0x1A;
    const long last = _lseek(fd, -1, SEEK_END);
    if (last == -1) return _doserrno == ERROR_NEGATIVE_SEEK;   // an empty file
    char c = 0;
    if (_read(fd, &c, 1) == 0 && c == kCtrlZ && _chsize_s(fd, last) != 0) return false;
    return _lseek(fd, 0, SEEK_SET) != -1;
}

// _sopen (006EB373): the flags to CreateFileA's access, sharing, disposition and attributes as VC6
// maps them; the handle becomes a host descriptor in the same translation mode.
int Sopen(const char* path, int flags, int share, int permissions) {
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, (flags & _O_NOINHERIT) ? FALSE : TRUE};
    const bool text = !(flags & _O_BINARY) && ((flags & _O_TEXT) || GameFileMode() != _O_BINARY);
    DWORD access;
    switch (flags & (_O_RDONLY | _O_WRONLY | _O_RDWR)) {
    case _O_RDONLY: access = GENERIC_READ; break;
    case _O_WRONLY: access = GENERIC_WRITE; break;
    case _O_RDWR: access = GENERIC_READ | GENERIC_WRITE; break;
    default: return -1;
    }
    DWORD sharing;
    switch (share) {
    case _SH_DENYRW: sharing = 0; break;
    case _SH_DENYWR: sharing = FILE_SHARE_READ; break;
    case _SH_DENYRD: sharing = FILE_SHARE_WRITE; break;
    case _SH_DENYNO: sharing = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
    default: return -1;
    }
    DWORD disposition;
    switch (flags & (_O_CREAT | _O_EXCL | _O_TRUNC)) {
    case _O_CREAT: disposition = OPEN_ALWAYS; break;
    case _O_CREAT | _O_EXCL:
    case _O_CREAT | _O_TRUNC | _O_EXCL: disposition = CREATE_NEW; break;
    case _O_TRUNC:
    case _O_TRUNC | _O_EXCL: disposition = TRUNCATE_EXISTING; break;
    case _O_CREAT | _O_TRUNC: disposition = CREATE_ALWAYS; break;
    default: disposition = OPEN_EXISTING; break;   // none, or _O_EXCL alone
    }
    DWORD attributes = FILE_ATTRIBUTE_NORMAL;
    if ((flags & _O_CREAT) && !(permissions & ~GameUmask() & _S_IWRITE)) attributes = FILE_ATTRIBUTE_READONLY;
    if (flags & _O_TEMPORARY) {
        attributes |= FILE_FLAG_DELETE_ON_CLOSE;
        access |= DELETE;
    }
    if (flags & _O_SHORT_LIVED) attributes |= FILE_ATTRIBUTE_TEMPORARY;
    if (flags & _O_SEQUENTIAL) {
        attributes |= FILE_FLAG_SEQUENTIAL_SCAN;
    } else if (flags & _O_RANDOM) {
        attributes |= FILE_FLAG_RANDOM_ACCESS;
    }
    const HANDLE handle = gameapi::CreateFileA(path, access, sharing, &security, disposition, attributes, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return -1;
    const DWORD type = gameapi::GetFileType(handle);
    if (type == FILE_TYPE_UNKNOWN) {
        gameapi::CloseHandle(handle);
        return -1;
    }
    const bool device = type == FILE_TYPE_CHAR || type == FILE_TYPE_PIPE;
    int hostFlags = (text ? _O_TEXT : 0) | (flags & _O_NOINHERIT);
    if (!device && (flags & _O_APPEND)) hostFlags |= _O_APPEND;
    QuietInvalidParameters quiet;
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle), hostFlags);
    if (fd == -1) {
        gameapi::CloseHandle(handle);
        return -1;
    }
    if (!device && text && (flags & _O_RDWR) && !DropTrailingCtrlZ(fd)) {
        _close(fd);
        return -1;
    }
    return fd;
}

// FILETIME to a local time_t (__timet_from_ft 006EB9E7): -1 for a zero time or one out of range.
std::int32_t LocalTime(const FILETIME& time) {
    if (time.dwLowDateTime == 0 && time.dwHighDateTime == 0) return -1;
    FILETIME local;
    SYSTEMTIME parts;
    if (!gameapi::FileTimeToLocalFileTime(&time, &local) || !gameapi::FileTimeToSystemTime(&local, &parts)) return -1;
    std::tm broken{};
    broken.tm_year = parts.wYear - 1900;
    broken.tm_mon = parts.wMonth - 1;
    broken.tm_mday = parts.wDay;
    broken.tm_hour = parts.wHour;
    broken.tm_min = parts.wMinute;
    broken.tm_sec = parts.wSecond;
    broken.tm_isdst = -1;
    return static_cast<std::int32_t>(_mktime32(&broken));
}

} // namespace

int __cdecl Open(const char* path, int flags, ...) {
    int permissions = 0;
    if (flags & _O_CREAT) {
        va_list args;
        va_start(args, flags);
        permissions = va_arg(args, int);
        va_end(args);
    }
    return Sopen(path, flags, _SH_DENYNO, permissions);
}
EI_REPLACE_STANDALONE(0x006EB35C, libcrt::Open);

int __cdecl Read(int fd, void* buffer, unsigned count) {
    QuietInvalidParameters quiet;
    return _read(fd, buffer, count);
}
EI_REPLACE_STANDALONE(0x006EB046, libcrt::Read);

int __cdecl Write(int fd, const void* buffer, unsigned count) {
    QuietInvalidParameters quiet;
    return _write(fd, buffer, count);
}
EI_REPLACE_STANDALONE(0x006EB642, libcrt::Write);

long __cdecl Lseek(int fd, long offset, int origin) {
    QuietInvalidParameters quiet;
    return _lseek(fd, offset, origin);
}
EI_REPLACE_STANDALONE(0x006EB284, libcrt::Lseek);

int __cdecl Close(int fd) {
    QuietInvalidParameters quiet;
    return _close(fd);
}
EI_REPLACE_STANDALONE(0x006EAF66, libcrt::Close);

long __cdecl Filelength(int fd) {
    QuietInvalidParameters quiet;
    return _filelength(fd);
}
EI_REPLACE_STANDALONE(0x006E904C, libcrt::Filelength);

// Only the write bit of the mode is tested: a read-only file fails it.
int __cdecl Access(const char* path, int mode) {
    constexpr int kWrite = 2;
    const DWORD attributes = gameapi::GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) return -1;
    if ((attributes & FILE_ATTRIBUTE_READONLY) && (mode & kWrite)) return -1;
    return 0;
}
EI_REPLACE_STANDALONE(0x006EAF20, libcrt::Access);

long __cdecl Findfirst(const char* pattern, gamecrt::FindData* data) {
    WIN32_FIND_DATAA found;
    const HANDLE handle = gameapi::FindFirstFileA(pattern, &found);
    if (handle == INVALID_HANDLE_VALUE) return -1;
    data->attrib = found.dwFileAttributes == FILE_ATTRIBUTE_NORMAL ? 0 : found.dwFileAttributes;
    data->createTime = LocalTime(found.ftCreationTime);
    data->accessTime = LocalTime(found.ftLastAccessTime);
    data->writeTime = LocalTime(found.ftLastWriteTime);
    data->size = found.nFileSizeLow;
    strcpy_s(data->name, found.cFileName);
    return static_cast<long>(reinterpret_cast<std::intptr_t>(handle));
}
EI_REPLACE_STANDALONE(0x006EB832, libcrt::Findfirst);

// _openfile (006F2811): the first letter is required (r, w or a); the letters after it are read up to
// one that is unknown or repeats its group, which ends the mode without failing it.
gamecrt::CrtFile* __cdecl Fopen(const char* path, const char* mode) {
    int flags;
    switch (mode[0]) {
    case 'r': flags = _O_RDONLY; break;
    case 'w': flags = _O_WRONLY | _O_CREAT | _O_TRUNC; break;
    case 'a': flags = _O_WRONLY | _O_CREAT | _O_APPEND; break;
    default: return nullptr;
    }
    bool commit = (GameCommitMode() & kStreamCommit) != 0;
    bool commitGiven = false;
    bool accessGiven = false;
    for (const char* p = mode + 1; *p; ++p) {
        bool taken = false;
        switch (*p) {
        case '+':
            taken = !(flags & _O_RDWR);
            if (taken) flags = (flags & ~_O_WRONLY) | _O_RDWR;
            break;
        case 'b':
        case 't':
            taken = !(flags & (_O_TEXT | _O_BINARY));
            if (taken) flags |= *p == 'b' ? _O_BINARY : _O_TEXT;
            break;
        case 'c':
        case 'n':
            taken = !commitGiven;
            if (taken) commit = *p == 'c';
            commitGiven = true;
            break;
        case 'S':
        case 'R':
            taken = !accessGiven;
            if (taken) flags |= *p == 'S' ? _O_SEQUENTIAL : _O_RANDOM;
            accessGiven = true;
            break;
        case 'T':
            taken = !(flags & _O_SHORT_LIVED);
            flags |= _O_SHORT_LIVED;
            break;
        case 'D':
            taken = !(flags & _O_TEMPORARY);
            flags |= _O_TEMPORARY;
            break;
        default: break;
        }
        if (!taken) break;
    }
    const int fd = Sopen(path, flags, _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (fd < 0) return nullptr;
    // The descriptor already has its translation mode and append flag; the stream takes the direction.
    char hostMode[4];
    char* m = hostMode;
    *m++ = mode[0];
    if (flags & _O_RDWR) *m++ = '+';
    if (commit) *m++ = 'c';
    *m = '\0';
    QuietInvalidParameters quiet;
    FILE* file = _fdopen(fd, hostMode);
    if (!file) {
        _close(fd);
        return nullptr;
    }
    return reinterpret_cast<gamecrt::CrtFile*>(file);
}
EI_REPLACE_STANDALONE(0x006E9107, libcrt::Fopen);

int __cdecl Fclose(gamecrt::CrtFile* file) {
    QuietInvalidParameters quiet;
    return std::fclose(HostFile(file));
}
EI_REPLACE_STANDALONE(0x006E8EB8, libcrt::Fclose);

int __cdecl Fseek(gamecrt::CrtFile* file, long offset, int origin) {
    QuietInvalidParameters quiet;
    return std::fseek(HostFile(file), offset, origin);
}
EI_REPLACE_STANDALONE(0x006E95D2, libcrt::Fseek);

long __cdecl Ftell(gamecrt::CrtFile* file) {
    QuietInvalidParameters quiet;
    return std::ftell(HostFile(file));
}
EI_REPLACE_STANDALONE(0x006E97C4, libcrt::Ftell);

std::size_t __cdecl Fread(void* to, std::size_t size, std::size_t count, gamecrt::CrtFile* file) {
    QuietInvalidParameters quiet;
    return std::fread(to, size, count, HostFile(file));
}
EI_REPLACE_STANDALONE(0x006E8F35, libcrt::Fread);

std::size_t __cdecl Fwrite(const void* from, std::size_t size, std::size_t count, gamecrt::CrtFile* file) {
    QuietInvalidParameters quiet;
    return std::fwrite(from, size, count, HostFile(file));
}
EI_REPLACE_STANDALONE(0x006E968B, libcrt::Fwrite);

// A null stream flushes every stream (VC6's flsall: 0, or EOF when one fails), as the host's does.
int __cdecl Fflush(gamecrt::CrtFile* file) {
    QuietInvalidParameters quiet;
    return std::fflush(file ? HostFile(file) : nullptr);
}
EI_REPLACE_STANDALONE(0x006ED534, libcrt::Fflush);

int Fileno(const gamecrt::CrtFile* file) {
    QuietInvalidParameters quiet;
    return _fileno(HostFile(file));
}

} // namespace libcrt

namespace gamecrt {

int fileno(const CrtFile* file) {
    if (ei::Standalone()) return libcrt::Fileno(file);
    // VC6's FILE: _ptr, _cnt, _base, _flag, then _file.
    struct GameFile {
        char* ptr;
        int count;
        char* base;
        int flag;
        int descriptor;
    };
    return reinterpret_cast<const GameFile*>(file)->descriptor;
}

} // namespace gamecrt
