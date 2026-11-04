// The platform's files on Linux and macOS (platform.h, "Files and folders"; docs/notes/platform.md, "Files and the
// harness's overlay"): the paths' native form with Windows' case rules, the file objects on descriptors, whole files,
// mappings and folder searches with Windows' wildcards. The folder calls are src/platform/files.cpp's (every system).
// Every path goes through SystemPath and every write through the write watch (seams.h), which the harness's builds
// answer.
#include "platform/platform.h"

#include "lib/text_case.h"
#include "platform/posix/platform_posix.h"
#include "platform/seams.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace platform {

namespace {

namespace fs = std::filesystem;

// A file object's handle as its descriptor (-1: none).
int Descriptor(std::intptr_t handle) { return static_cast<int>(handle); }

[[noreturn]] void FileFailure(std::string_view path) { throw FileError(errno, path); }

// `path` for `use` as the system names it (seams.h).
fs::path SystemNative(std::string_view path, PathUse use) { return NativePath(SystemPath(path, use)); }

bool Matches(std::string_view pattern, std::string_view value) {
    if (pattern == "*.*") pattern = "*";
    std::size_t p = 0, v = 0, star = pattern.npos, retry = 0;
    while (v < value.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || text::ToLower(pattern[p]) == text::ToLower(value[v]))) {
            ++p;
            ++v;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            retry = v;
        } else if (star != pattern.npos) {
            p = star + 1;
            v = ++retry;
        } else return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

} // namespace

// Asset names use backslashes and Windows case rules, including on case-sensitive volumes.
fs::path NativePath(std::string_view path) {
    std::string native(path);
    std::replace(native.begin(), native.end(), '\\', '/');
    fs::path resolved;
    for (const auto& part : fs::path(native)) {
        if (part == "/" || part == "." || part == "..") {
            resolved /= part;
            continue;
        }
        const fs::path exact = resolved / part;
        std::error_code error;
        if (fs::exists(exact, error)) {
            resolved = exact;
            continue;
        }
        fs::path found;
        for (const auto& entry : fs::directory_iterator(resolved.empty() ? fs::path(".") : resolved, error)) {
            if (text::CompareIgnoringCase(entry.path().filename().string(), part.string()) != 0) continue;
            // A stable choice if a volume contains two names that Windows would treat as identical.
            if (found.empty() || entry.path().filename().string() < found.filename().string()) found = entry.path();
        }
        resolved = found.empty() ? exact : found;
    }
    return resolved;
}

// As Windows' text (win32/files.cpp), the system's own words added.
FileError::FileError(unsigned long code, std::string_view path)
    : std::runtime_error("file error: OS error " + std::to_string(code) + " (" + std::strerror(static_cast<int>(code)) +
                         "), file '" + std::string(path) + "'") {}

// ---- Whole files and folder searches -----------------------------------------------------------------------------

bool ReadWholeFile(std::string_view path, std::string* bytes, std::uint32_t* error) {
    bytes->clear();
    errno = 0;
    try {
        File file;
        if (file.Open(path, FileMode::Read)) {
            bytes->resize(file.Size());
            if (file.ReadAt(0, bytes->data(), static_cast<unsigned>(bytes->size()))) {
                if (error) *error = 0;
                return true;
            }
        }
    } catch (const FileError&) {
    }
    if (error) *error = errno ? errno : EIO;
    bytes->clear();
    return false;
}

bool WriteWholeFile(std::string_view path, const std::string& bytes, std::uint32_t* error) {
    const auto target = SystemNative(path, PathUse::Write);
    std::error_code e;
    if (!target.parent_path().empty()) fs::create_directories(target.parent_path(), e);
    std::string temp = target.string() + ".XXXXXX";
    const int fd = mkstemp(temp.data());
    if (fd < 0) {
        if (error) *error = errno;
        return false;
    }
    bool ok = false;
    const auto* data = bytes.data();
    std::size_t left = bytes.size();
    while (left) {
        const ssize_t n = write(fd, data, left);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        data += n;
        left -= static_cast<std::size_t>(n);
    }
    ok = left == 0 && close(fd) == 0 && rename(temp.c_str(), target.c_str()) == 0;
    if (left) close(fd);
    const int saved = errno;
    unlink(temp.c_str());
    if (error) *error = ok ? 0 : saved;
    return ok;
}

// The pattern's folder through the mapping, its entries matched with Windows' wildcards and case rules, in NTFS's name
// order (Windows' order on its volumes); no "." or "..".
std::vector<FolderEntry> FolderEntries(std::string_view pattern) {
    // The folder in the game's form (backslashes), as every other call asks SystemPath for its path.
    const std::string game(pattern);
    const std::size_t slash = game.find_last_of("\\/");
    const std::string folderName = slash == std::string::npos ? "." : game.substr(0, slash == 0 ? 1 : slash);
    const fs::path folder = SystemNative(folderName, PathUse::Read);
    const std::string mask = slash == std::string::npos ? game : game.substr(slash + 1);
    std::error_code e;
    const std::string root = posix::GamePath(fs::absolute(NativePath(folderName), e));
    std::vector<fs::directory_entry> found;
    for (const auto& entry : fs::directory_iterator(folder, e))
        if (Matches(mask, entry.path().filename().string())) found.push_back(entry);
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        return text::CompareIgnoringCase(a.path().filename().string(), b.path().filename().string()) < 0;
    });
    std::vector<FolderEntry> entries;
    for (const fs::directory_entry& entry : found) {
        FolderEntry& out = entries.emplace_back();
        out.name = entry.path().filename().string();
        out.path = root + "\\" + out.name;
        out.folder = entry.is_directory(e);
        out.size = out.folder ? 0 : static_cast<std::uint32_t>(entry.file_size(e));
    }
    return entries;
}

// ---- File --------------------------------------------------------------------------------------------------------

File::~File() { Close(); }

bool File::Open(std::string_view filePath, FileMode mode) {
    Close();
    path = filePath;
    const int flags = mode == FileMode::Read     ? O_RDONLY
                      : mode == FileMode::Create ? O_WRONLY | O_CREAT | O_TRUNC
                                                 : O_WRONLY;
    const PathUse use = mode == FileMode::Read ? PathUse::Read : mode == FileMode::Create ? PathUse::Write : PathUse::Update;
    handle = open(SystemNative(path, use).c_str(), flags | O_CLOEXEC, 0666);
    return IsOpen();
}

void File::Close() {
    if (IsOpen()) close(Descriptor(handle));
    handle = -1;
}

unsigned File::Read(void* buffer, unsigned count) {
    ssize_t n;
    do {
        n = read(Descriptor(handle), buffer, count);
    } while (n < 0 && errno == EINTR);
    if (n < 0) FileFailure(path);
    return static_cast<unsigned>(n);
}

// The write watch first (seams.h): the harness's builds fail a write as on a full disk, and hear of each failed one.
void File::Write(const void* buffer, unsigned count) {
    if (count == 0) return;
    const bool injected = WriteFailsAsFull(path);
    unsigned long error = ENOSPC;
    const auto* bytes = static_cast<const char*>(buffer);
    unsigned done = 0;
    while (!injected && done < count) {
        const ssize_t n = write(Descriptor(handle), bytes + done, count - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            if (n < 0) error = static_cast<unsigned long>(errno);
            break;
        }
        done += static_cast<unsigned>(n);
    }
    if (done == count) return;
    WriteFailed(path, handle, count, error, injected);
    throw FileError(error, path);
}

void File::WriteLine(std::string_view line) {
    Write(line.data(), static_cast<unsigned>(line.size()));
    Write("\r\n", 2);
}

long File::Seek(long offset, SeekFrom from) {
    const off_t n = lseek(Descriptor(handle), offset,
                          from == SeekFrom::Begin ? SEEK_SET
                          : from == SeekFrom::End ? SEEK_END
                                                  : SEEK_CUR);
    if (n < 0 || n > UINT32_MAX) FileFailure(path);
    return static_cast<long>(n);
}

std::uint32_t File::Position() const {
    const off_t n = lseek(Descriptor(handle), 0, SEEK_CUR);
    if (n < 0 || n > UINT32_MAX) FileFailure(path);
    return static_cast<std::uint32_t>(n);
}

std::uint32_t File::Size() const {
    struct stat s{};
    if (fstat(Descriptor(handle), &s) || s.st_size < 0 || s.st_size > UINT32_MAX) FileFailure(path);
    return static_cast<std::uint32_t>(s.st_size);
}

bool File::ReadAt(long offset, void* buffer, unsigned count) {
    // Preserve the stream-position side effect of the Windows implementation.
    try {
        Seek(offset, SeekFrom::Begin);
        return Read(buffer, count) == count;
    } catch (const FileError&) {
        return false;
    }
}

// ---- MappedFile --------------------------------------------------------------------------------------------------

MappedFile::~MappedFile() { Close(); }

bool MappedFile::Open(std::string_view path) {
    Close();
    const int fd = open(SystemNative(path, PathUse::Read).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    file = fd;
    struct stat s{};
    if (fstat(fd, &s) || s.st_size <= 0 || s.st_size > UINT32_MAX) {
        Close();
        return false;
    }
    size = static_cast<std::uint32_t>(s.st_size);
    void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        Close();
        return false;
    }
    view = mapped;
    return true;
}

void MappedFile::Close() {
    if (view) munmap(const_cast<void*>(view), size);
    if (file != -1) close(Descriptor(file));
    file = -1;
    view = nullptr;
    size = 0;
}

} // namespace platform
