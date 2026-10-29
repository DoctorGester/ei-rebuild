#include "file_io.h"

#include "gameapi.h"

#include <windows.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <io.h>

namespace lib {

namespace {

std::string ErrorText(unsigned long osError, std::string_view path) {
    return "file error: OS error " + std::to_string(osError) + ", file '" + std::string(path) + "'";
}

[[noreturn]] void ThrowLastError(std::string_view path) { throw FileError(gameapi::GetLastError(), path); }
// The host CRT's Win32 error of its last failed call.
[[noreturn]] void ThrowCrtError(std::string_view path) { throw FileError(static_cast<unsigned long>(_doserrno), path); }

// A closed file's calls fail as on INVALID_HANDLE_VALUE (MFC's hFileNull).
HANDLE Win32(void* handle) { return handle ? handle : INVALID_HANDLE_VALUE; }

} // namespace

FileError::FileError(unsigned long error, std::string_view path) : std::runtime_error(ErrorText(error, path)), osError(error) {}

// ---- File -----------------------------------------------------------------------------------------

File::File(std::string_view path, unsigned flags) {
    if (!Open(path, flags)) ThrowLastError(path);
}

File::~File() { Close(); }

bool File::Open(std::string_view filePath, unsigned flags) {
    Close();
    path = filePath;
    DWORD access = 0;
    switch (flags & 3) {
    case modeRead: access = GENERIC_READ; break;
    case modeWrite: access = GENERIC_WRITE; break;
    case modeReadWrite: access = GENERIC_READ | GENERIC_WRITE; break;
    default: break;
    }
    DWORD share = 0;   // exclusive (and the undefined values)
    switch (flags & 0x70) {
    case shareDenyWrite: share = FILE_SHARE_READ; break;
    case shareDenyRead: share = FILE_SHARE_WRITE; break;
    case shareDenyNone: share = FILE_SHARE_READ | FILE_SHARE_WRITE; break;
    default: break;
    }
    DWORD disposition = OPEN_EXISTING;
    if (flags & modeCreate) disposition = (flags & modeNoTruncate) ? OPEN_ALWAYS : CREATE_ALWAYS;
    const HANDLE opened =
        gameapi::CreateFileA(path.c_str(), access, share, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    handle = opened == INVALID_HANDLE_VALUE ? nullptr : opened;
    return IsOpen();
}

void File::Close() {
    if (IsOpen()) gameapi::CloseHandle(handle);
    handle = nullptr;
}

unsigned File::Read(void* buffer, unsigned count) {
    if (count == 0) return 0;
    DWORD read;
    if (!gameapi::ReadFile(Win32(handle), buffer, count, &read, nullptr)) ThrowLastError(path);
    return read;
}

void File::Write(const void* buffer, unsigned count) {
    if (count == 0) return;
    DWORD written;
    if (!gameapi::WriteFile(Win32(handle), buffer, count, &written, nullptr)) ThrowLastError(path);
    if (written != count) throw FileError(ERROR_DISK_FULL, path);
}

long File::Seek(long offset, unsigned from) {
    const DWORD position = gameapi::SetFilePointer(Win32(handle), offset, nullptr, from);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path);
    return static_cast<long>(position);
}

unsigned long File::GetPosition() const {
    const DWORD position = gameapi::SetFilePointer(Win32(handle), 0, nullptr, FILE_CURRENT);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path);
    return position;
}

unsigned long File::GetLength() const {
    const DWORD size = gameapi::GetFileSize(Win32(handle), nullptr);
    if (size == INVALID_FILE_SIZE) ThrowLastError(path);
    return size;
}

// ---- TextFile -------------------------------------------------------------------------------------

TextFile::~TextFile() { Close(); }

bool TextFile::Open(std::string_view filePath, const char* mode) {
    Close();
    path = filePath;
    stream = OpenFile(path, mode);
    return stream != nullptr;
}

void TextFile::Close() {
    if (stream) std::fclose(stream);
    stream = nullptr;
}

// Read 128 characters at a time (fgets is given 129, as MFC's ReadString read into the string's buffer).
bool TextFile::ReadLine(std::string& line) {
    constexpr int kPiece = 128;
    char piece[kPiece + 1];
    line.clear();
    char* result;
    for (;;) {
        result = std::fgets(piece, kPiece + 1, stream);
        if (!result) {
            if (!std::feof(stream)) {
                std::clearerr(stream);
                ThrowCrtError(path);
            }
            break;
        }
        const std::size_t length = std::strlen(piece);
        line.append(piece, length);
        if (length < kPiece || piece[length - 1] == '\n') break;
    }
    if (!line.empty() && line.back() == '\n') line.pop_back();
    return result != nullptr;
}

char* TextFile::ReadString(char* buffer, unsigned size) {
    char* result = std::fgets(buffer, static_cast<int>(size), stream);
    if (!result && !std::feof(stream)) {
        std::clearerr(stream);
        ThrowCrtError(path);
    }
    return result;
}

void TextFile::WriteString(std::string_view text) {
    if (std::fwrite(text.data(), 1, text.size(), stream) != text.size()) ThrowCrtError(path);
}

// ---- FileFind -------------------------------------------------------------------------------------

FileFind::FileFind() = default;
FileFind::~FileFind() { Close(); }

void FileFind::Close() {
    if (context) gameapi::FindClose(context);
    context = nullptr;
    hasFound = false;
}

bool FileFind::FindFile(std::string_view pattern) {
    Close();
    const std::string text(pattern);
    if (!next) next = std::make_unique<WIN32_FIND_DATAA>();
    const HANDLE found = gameapi::FindFirstFileA(text.c_str(), next.get());
    if (found == INVALID_HANDLE_VALUE) return false;
    context = found;
    // The root: the pattern made absolute (the current folder for an empty one), up to its last separator.
    char full[MAX_PATH];
    const DWORD length = text.empty() ? gameapi::GetCurrentDirectoryA(MAX_PATH, full)
                                      : gameapi::GetFullPathNameA(text.c_str(), MAX_PATH, full, nullptr);
    if (length == 0 || length >= MAX_PATH) {
        Close();
        gameapi::SetLastError(ERROR_INVALID_NAME);
        return false;
    }
    root.assign(full, length);
    if (const std::size_t cut = root.find_last_of("\\/"); cut != std::string::npos) root.resize(cut);
    return true;
}

bool FileFind::FindNext() {
    if (!context) return false;
    hasFound = true;
    name = next->cFileName;
    attributes = next->dwFileAttributes;
    size = next->nFileSizeLow;
    return gameapi::FindNextFileA(context, next.get()) != 0;
}

std::string FileFind::GetFileName() const { return hasFound ? name : std::string(); }

std::string FileFind::GetFilePath() const {
    std::string path = root;
    if (path.empty() || (path.back() != '\\' && path.back() != '/')) path += '\\';
    return path + GetFileName();
}

std::string FileFind::GetTitle() const {
    const std::string path = GetFilePath();
    char title[MAX_PATH] = {};
    _splitpath_s(path.c_str(), nullptr, 0, nullptr, 0, title, MAX_PATH, nullptr, 0);
    return title;
}

unsigned long FileFind::GetLength() const { return hasFound ? size : 0; }

bool FileFind::MatchesMask(unsigned long mask) const { return hasFound && (attributes & mask) != 0; }

bool FileFind::IsDots() const { return MatchesMask(FILE_ATTRIBUTE_DIRECTORY) && (name == "." || name == ".."); }

// ---- fopen / _access ------------------------------------------------------------------------------

std::FILE* OpenFile(const std::string& path, const char* mode) {
    const char kind = mode[0];
    if (kind != 'r' && kind != 'w' && kind != 'a') return nullptr;
    const bool update = std::strchr(mode + 1, '+') != nullptr;
    const bool binary = std::strchr(mode + 1, 'b') != nullptr;
    const DWORD access = update ? GENERIC_READ | GENERIC_WRITE : kind == 'r' ? GENERIC_READ : GENERIC_WRITE;
    const DWORD disposition = kind == 'r' ? OPEN_EXISTING : kind == 'w' ? CREATE_ALWAYS : OPEN_ALWAYS;
    const HANDLE handle = gameapi::CreateFileA(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disposition,
                                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return nullptr;
    // The descriptor takes the translation mode and the append flag, the stream the direction.
    const int fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), (binary ? 0 : _O_TEXT) | (kind == 'a' ? _O_APPEND : 0));
    if (fd == -1) {
        CloseHandle(handle);
        return nullptr;
    }
    const char streamMode[3] = {kind, update ? '+' : '\0', '\0'};
    std::FILE* file = _fdopen(fd, streamMode);
    if (!file) _close(fd);
    return file;
}

bool FileExists(const std::string& path) { return gameapi::GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

} // namespace lib
