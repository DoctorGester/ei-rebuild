// The platform's files on Windows (platform.h, "Files and folders"; docs/notes/platform.md, "Files and the harness's
// overlay"): whole files, folder searches, the file objects and the paths' native form, on the wide calls. The folder
// calls are src/platform/files.cpp's (every system). Every path goes through SystemPath and every write through the
// write watch (seams.h), which the harness's builds answer.
#include "platform/platform.h"

#include "platform/path_layers.h"
#include "platform/seams.h"
#include "platform/win32/platform_win32.h"
#include "platform/zip_folders.h"

#include <string>

namespace platform {

namespace {

// `path` as the system's wide path for `use`.
std::wstring SystemWide(std::string_view path, PathUse use) { return text::Wide(SystemPath(path, use)); }

std::string ErrorText(unsigned long code, std::string_view path) {
    return "file error: OS error " + std::to_string(code) + ", file '" + std::string(path) + "'";
}

[[noreturn]] void ThrowLastError(std::string_view path) { throw FileError(::GetLastError(), path); }

// A file object's handle as the system's: -1, no file, is INVALID_HANDLE_VALUE, on which every call fails.
HANDLE Native(std::intptr_t handle) { return reinterpret_cast<HANDLE>(handle); }

// The system's `mapped` opened with `access` and `disposition` (CreateFile's), shared with other readers and writers;
// -1 when it does not open.
std::intptr_t OpenFile(const std::string& mapped, DWORD access, DWORD disposition) {
    return reinterpret_cast<std::intptr_t>(CreateFileW(text::Wide(mapped).c_str(), access,
                                                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, disposition,
                                                       FILE_ATTRIBUTE_NORMAL, nullptr));
}

// `path` made absolute (none when the call fails).
std::wstring FullPath(const std::wstring& path) {
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    std::wstring full(needed, L'\0');
    const DWORD length = needed ? GetFullPathNameW(path.c_str(), needed, full.data(), nullptr) : 0;
    full.resize(length < needed ? length : 0);
    return full;
}

} // namespace

std::filesystem::path NativePath(std::string_view path) { return std::filesystem::path(text::Wide(path)); }

FileError::FileError(unsigned long code, std::string_view path) : std::runtime_error(ErrorText(code, path)) {}

// ---- Whole files and folder searches -----------------------------------------------------------------------------

// Shared with a rename or a delete too: the file is closed before the call returns.
bool ReadWholeFile(std::string_view path, std::string* bytes, std::uint32_t* error) {
    bytes->clear();
    const std::string mapped = SystemPath(path, PathUse::Read);
    if (std::shared_ptr<const std::string> zipped; ZipFile(mapped, &zipped)) {
        if (zipped) *bytes = *zipped;
        if (error) *error = zipped ? 0 : ERROR_FILE_NOT_FOUND;
        return zipped != nullptr;
    }
    const HANDLE handle = CreateFileW(text::Wide(mapped).c_str(), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0,
                                      nullptr);
    bool ok = handle != INVALID_HANDLE_VALUE;
    LARGE_INTEGER size{};
    if (ok) ok = GetFileSizeEx(handle, &size) && size.HighPart == 0;
    if (ok) {
        bytes->resize(size.LowPart);
        DWORD read = 0;
        ok = ::ReadFile(handle, bytes->data(), size.LowPart, &read, nullptr) && read == size.LowPart;
    }
    if (error) *error = ok ? 0 : GetLastError();
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    if (!ok) bytes->clear();
    return ok;
}

bool WriteWholeFile(std::string_view path, const std::string& bytes, std::uint32_t* error) {
    const std::wstring file = SystemWide(path, PathUse::Write);
    const std::wstring temporary = file + L".tmp";
    CreateDirectoryW(file.substr(0, file.find_last_of(L'\\')).c_str(), nullptr);
    HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    bool ok = handle != INVALID_HANDLE_VALUE;
    if (ok) {
        ok = ::WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
             written == bytes.size();
        CloseHandle(handle);
    }
    ok = ok && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (error) *error = ok ? 0 : GetLastError();
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

// A folder in a zip (zip_folders.h) is searched there, "." and ".." first as on NTFS.
std::vector<FolderEntry> SearchFolder(std::string_view systemPattern, std::string_view pattern) {
    std::vector<FolderEntry> entries;
    // The entries' folder: the pattern as the game named it made absolute (the current folder for an empty one), up to
    // its last separator; a path of MAX_PATH bytes or more finds nothing.
    std::string root = pattern.empty() ? CurrentFolder() : text::Narrow(FullPath(text::Wide(pattern)));
    if (root.empty() || root.size() >= MAX_PATH) return entries;
    if (const std::size_t cut = root.find_last_of("\\/"); cut != std::string::npos) root.resize(cut);
    if (root.empty() || (root.back() != '\\' && root.back() != '/')) root += '\\';
    if (SearchZip(systemPattern, root, true, &entries)) return entries;
    WIN32_FIND_DATAW found;
    const HANDLE find = FindFirstFileW(text::Wide(systemPattern).c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return entries;
    do {
        FolderEntry& entry = entries.emplace_back();
        entry.name = text::Narrow(found.cFileName);
        entry.path = root + entry.name;
        entry.size = found.nFileSizeLow;
        entry.folder = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    } while (FindNextFileW(find, &found));
    ::FindClose(find);
    return entries;
}

// ---- File --------------------------------------------------------------------------------------------------------

File::~File() { Close(); }

// A zip's file (zip_folders.h) opens for reading only; a write into a zip fails as into a missing folder.
bool File::Open(std::string_view filePath, FileMode mode) {
    Close();
    path = filePath;
    const PathUse use = mode == FileMode::Read ? PathUse::Read : mode == FileMode::Create ? PathUse::Write : PathUse::Update;
    const std::string mapped = SystemPath(path, use);
    if (ZipFile(mapped, &zipped)) {
        if (mode != FileMode::Read) zipped = nullptr;
        return IsOpen();
    }
    const DWORD access = mode == FileMode::Read ? GENERIC_READ : GENERIC_WRITE;
    handle = OpenFile(mapped, access, mode == FileMode::Create ? CREATE_ALWAYS : OPEN_EXISTING);
    return IsOpen();
}

void File::Close() {
    if (handle != -1) ::CloseHandle(Native(handle));
    handle = -1;
    zipped = nullptr;
    zippedAt = 0;
}

unsigned File::Read(void* buffer, unsigned count) {
    if (zipped) return ReadZipped(*zipped, &zippedAt, buffer, count);
    if (count == 0) return 0;
    DWORD read;
    if (!::ReadFile(Native(handle), buffer, count, &read, nullptr)) ThrowLastError(path);
    return read;
}

// The write watch first (seams.h): the harness's builds fail a write as on a full disk, and hear of each failed one.
void File::Write(const void* buffer, unsigned count) {
    if (count == 0) return;
    const bool injected = WriteFailsAsFull(path);
    DWORD written = 0;
    if (!injected && ::WriteFile(Native(handle), buffer, count, &written, nullptr)) {
        if (written != count) throw FileError(ERROR_DISK_FULL, path);
        return;
    }
    const unsigned long error = injected ? ERROR_DISK_FULL : ::GetLastError();
    WriteFailed(path, handle, count, error, injected);
    throw FileError(error, path);
}

void File::WriteLine(std::string_view line) {
    std::string text(line);
    text += "\r\n";
    Write(text.data(), static_cast<unsigned>(text.size()));
}

std::uint32_t File::Seek(std::int32_t offset, SeekFrom from) {
    if (zipped) {
        if (!SeekZipped(static_cast<std::uint32_t>(zipped->size()), &zippedAt, offset, from))
            throw FileError(ERROR_NEGATIVE_SEEK, path);
        return zippedAt;
    }
    const DWORD method = from == SeekFrom::Begin ? FILE_BEGIN : from == SeekFrom::Current ? FILE_CURRENT : FILE_END;
    const DWORD position = ::SetFilePointer(Native(handle), offset, nullptr, method);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path);
    return position;
}

std::uint32_t File::Position() const {
    if (zipped) return zippedAt;
    const DWORD position = ::SetFilePointer(Native(handle), 0, nullptr, FILE_CURRENT);
    if (position == INVALID_SET_FILE_POINTER) ThrowLastError(path);
    return position;
}

std::uint32_t File::Size() const {
    if (zipped) return static_cast<std::uint32_t>(zipped->size());
    const DWORD size = ::GetFileSize(Native(handle), nullptr);
    if (size == INVALID_FILE_SIZE) ThrowLastError(path);
    return size;
}

bool File::ReadAt(std::uint32_t position, void* buffer, unsigned count) {
    if (zipped) {
        zippedAt = position;
        return ReadZipped(*zipped, &zippedAt, buffer, count) == count;
    }
    DWORD read = 0;
    return ::SetFilePointer(Native(handle), static_cast<LONG>(position), nullptr, FILE_BEGIN) !=
               INVALID_SET_FILE_POINTER &&
           ::ReadFile(Native(handle), buffer, count, &read, nullptr) && read == count;
}

// ---- MappedFile --------------------------------------------------------------------------------------------------

MappedFile::~MappedFile() { Close(); }

// A zip's file (zip_folders.h): its bytes.
bool MappedFile::Open(std::string_view path) {
    Close();
    const std::string mapped = SystemPath(path, PathUse::Read);
    if (ZipFile(mapped, &zipped)) {
        if (!zipped || zipped->empty()) {
            Close();
            return false;
        }
        view = zipped->data();
        size = static_cast<std::uint32_t>(zipped->size());
        return true;
    }
    file = OpenFile(mapped, GENERIC_READ, OPEN_EXISTING);
    if (file == -1) return false;
    size = ::GetFileSize(Native(file), nullptr);
    if (size != INVALID_FILE_SIZE) mapping = ::CreateFileMappingW(Native(file), nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping) view = ::MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view) return true;
    Close();
    return false;
}

void MappedFile::Close() {
    if (view && !zipped) ::UnmapViewOfFile(view);
    zipped = nullptr;
    view = nullptr;
    if (mapping) ::CloseHandle(mapping);
    mapping = nullptr;
    if (file != -1) ::CloseHandle(Native(file));
    file = -1;
    size = 0;
}

} // namespace platform
