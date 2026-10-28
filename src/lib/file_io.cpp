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

bool File::Open(std::string_view path, unsigned flags) {
    Close();
    path_ = path;
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
    const HANDLE handle =
        gameapi::CreateFileA(path_.c_str(), access, share, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    handle_ = handle == INVALID_HANDLE_VALUE ? nullptr : handle;
    return IsOpen();
}

void File::Close() {
    if (IsOpen()) gameapi::CloseHandle(handle_);
    handle_ = nullptr;
}

unsigned File::Read(void* buffer, unsigned count) {
    if (count == 0) return 0;
    DWORD read;
    if (!gameapi::ReadFile(Win32(handle_), buffer, count, &read, nullptr)) ThrowLastError(path_);
    return read;
}

void File::Write(const void* buffer, unsigned count) {
    if (count == 0) return;
    DWORD written;
    if (!gameapi::WriteFile(Win32(handle_), buffer, count, &written, nullptr)) ThrowLastError(path_);
    if (written != count) throw FileError(ERROR_DISK_FULL, path_);
}

long File::Seek(long offset, unsigned from) {
    const DWORD position = gameapi::SetFilePointer(Win32(handle_), offset, nullptr, from);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path_);
    return static_cast<long>(position);
}

unsigned long File::GetPosition() const {
    const DWORD position = gameapi::SetFilePointer(Win32(handle_), 0, nullptr, FILE_CURRENT);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path_);
    return position;
}

unsigned long File::GetLength() const {
    const DWORD size = gameapi::GetFileSize(Win32(handle_), nullptr);
    if (size == INVALID_FILE_SIZE) ThrowLastError(path_);
    return size;
}

// ---- TextFile -------------------------------------------------------------------------------------

TextFile::~TextFile() { Close(); }

bool TextFile::Open(std::string_view path, const char* mode) {
    Close();
    path_ = path;
    stream_ = OpenFile(path_.c_str(), mode);
    return stream_ != nullptr;
}

void TextFile::Close() {
    if (stream_) std::fclose(stream_);
    stream_ = nullptr;
}

// Read 128 characters at a time (fgets is given 129, as MFC's ReadString read into the string's buffer).
bool TextFile::ReadLine(std::string& line) {
    constexpr int kPiece = 128;
    char piece[kPiece + 1];
    line.clear();
    char* result;
    for (;;) {
        result = std::fgets(piece, kPiece + 1, stream_);
        if (!result) {
            if (!std::feof(stream_)) {
                std::clearerr(stream_);
                ThrowCrtError(path_);
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
    char* result = std::fgets(buffer, static_cast<int>(size), stream_);
    if (!result && !std::feof(stream_)) {
        std::clearerr(stream_);
        ThrowCrtError(path_);
    }
    return result;
}

void TextFile::WriteString(std::string_view text) {
    if (std::fwrite(text.data(), 1, text.size(), stream_) != text.size()) ThrowCrtError(path_);
}

// ---- FileFind -------------------------------------------------------------------------------------

FileFind::FileFind() = default;
FileFind::~FileFind() { Close(); }

void FileFind::Close() {
    if (context_) gameapi::FindClose(context_);
    context_ = nullptr;
    hasFound_ = false;
}

bool FileFind::FindFile(std::string_view pattern) {
    Close();
    const std::string text(pattern);
    if (!next_) next_ = std::make_unique<WIN32_FIND_DATAA>();
    const HANDLE context = gameapi::FindFirstFileA(text.c_str(), next_.get());
    if (context == INVALID_HANDLE_VALUE) return false;
    context_ = context;
    // The root: the pattern made absolute (the current folder for an empty one), up to its last separator.
    char full[MAX_PATH];
    const DWORD length = text.empty() ? gameapi::GetCurrentDirectoryA(MAX_PATH, full)
                                      : gameapi::GetFullPathNameA(text.c_str(), MAX_PATH, full, nullptr);
    if (length == 0 || length >= MAX_PATH) {
        Close();
        gameapi::SetLastError(ERROR_INVALID_NAME);
        return false;
    }
    root_.assign(full, length);
    if (const std::size_t cut = root_.find_last_of("\\/"); cut != std::string::npos) root_.resize(cut);
    return true;
}

bool FileFind::FindNext() {
    if (!context_) return false;
    hasFound_ = true;
    name_ = next_->cFileName;
    attributes_ = next_->dwFileAttributes;
    size_ = next_->nFileSizeLow;
    return gameapi::FindNextFileA(context_, next_.get()) != 0;
}

std::string FileFind::GetFileName() const { return hasFound_ ? name_ : std::string(); }

std::string FileFind::GetFilePath() const {
    std::string path = root_;
    if (path.empty() || (path.back() != '\\' && path.back() != '/')) path += '\\';
    return path + GetFileName();
}

std::string FileFind::GetTitle() const {
    const std::string path = GetFilePath();
    char title[MAX_PATH] = {};
    _splitpath_s(path.c_str(), nullptr, 0, nullptr, 0, title, MAX_PATH, nullptr, 0);
    return title;
}

unsigned long FileFind::GetLength() const { return hasFound_ ? size_ : 0; }

bool FileFind::MatchesMask(unsigned long mask) const { return hasFound_ && (attributes_ & mask) != 0; }

bool FileFind::IsDots() const { return MatchesMask(FILE_ATTRIBUTE_DIRECTORY) && (name_ == "." || name_ == ".."); }

// ---- fopen / _access ------------------------------------------------------------------------------

std::FILE* OpenFile(const char* path, const char* mode) {
    const char kind = mode[0];
    if (kind != 'r' && kind != 'w' && kind != 'a') return nullptr;
    const bool update = std::strchr(mode + 1, '+') != nullptr;
    const bool binary = std::strchr(mode + 1, 'b') != nullptr;
    const DWORD access = update ? GENERIC_READ | GENERIC_WRITE : kind == 'r' ? GENERIC_READ : GENERIC_WRITE;
    const DWORD disposition = kind == 'r' ? OPEN_EXISTING : kind == 'w' ? CREATE_ALWAYS : OPEN_ALWAYS;
    const HANDLE handle = gameapi::CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disposition,
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

bool FileExists(const char* path) { return gameapi::GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES; }

} // namespace lib
