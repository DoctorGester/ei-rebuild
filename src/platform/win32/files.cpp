// Folders, drives and whole files on Windows (platform/files.h). The folder and file calls and the volume label go
// through game.exe's import slots (the harness redirects folder creation and the saves; the image routes them to the
// UTF-8 calls); the free space was never a slot (the original looked GetDiskFreeSpaceExA up by name) and calls the
// system's wide function; IsFile and FolderEntries call the UTF-8 versions (system_text.h) directly.
#include "platform/files.h"

#include "platform/win32/system_text.h"
#include "lib/gameapi.h"
#include "platform/win32/utf16.h"

#include <cstdint>
#include <windows.h>

namespace platform {

bool CreateFolder(const char* path) { return gameapi::CreateDirectoryA(path, nullptr) != FALSE; }

bool RemoveFile(const char* path) { return gameapi::DeleteFileA(path) != FALSE; }

bool RenameFile(const char* from, const char* to) { return gameapi::MoveFileA(from, to) != FALSE; }

bool CopyFileOver(const char* from, const char* to) { return gameapi::CopyFileA(from, to, FALSE) != FALSE; }

bool RemoveFolder(const char* path) { return gameapi::RemoveDirectoryA(path) != FALSE; }

bool PathExists(const std::string& path) {
    return GetFileAttributesW(win32::Utf16(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsFile(const std::string& path) {
    const DWORD attributes = text::utf8::GetFileAttributesA(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

WholeFile ReadWholeFile(const std::string& path, std::string* bytes, std::uint32_t* error) {
    const std::wstring wide = win32::Utf16(path);
    HANDLE handle = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        *error = GetLastError();
        return *error == ERROR_FILE_NOT_FOUND || *error == ERROR_PATH_NOT_FOUND ? WholeFile::missing : WholeFile::failed;
    }
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(handle, &size) && size.QuadPart < (1 << 24);
    if (ok) {
        bytes->resize(static_cast<std::size_t>(size.QuadPart));
        DWORD read = 0;
        ok = ReadFile(handle, bytes->data(), static_cast<DWORD>(bytes->size()), &read, nullptr) && read == bytes->size();
    }
    *error = ok ? 0 : GetLastError();
    CloseHandle(handle);
    return ok ? WholeFile::read : WholeFile::failed;
}

bool WriteWholeFile(const std::string& path, const std::string& bytes, std::uint32_t* error) {
    const std::wstring file = win32::Utf16(path);
    const std::wstring temporary = file + L".tmp";
    CreateDirectoryW(file.substr(0, file.find_last_of(L'\\')).c_str(), nullptr);
    HANDLE handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    bool ok = handle != INVALID_HANDLE_VALUE;
    if (ok) {
        ok = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
        CloseHandle(handle);
    }
    ok = ok && MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        *error = GetLastError();
        DeleteFileW(temporary.c_str());
    }
    return ok;
}

std::vector<FolderEntry> FolderEntries(const std::string& pattern) {
    std::vector<FolderEntry> entries;
    WIN32_FIND_DATAA found;
    const HANDLE find = text::utf8::FindFirstFileA(pattern.c_str(), &found);
    if (find == INVALID_HANDLE_VALUE) return entries;
    do {
        entries.push_back({found.cFileName, (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0});
    } while (text::utf8::FindNextFileA(find, &found));
    ::FindClose(find);
    return entries;
}

double FreeDiskSpace(const char* path) {
    ULARGE_INTEGER available;
    if (!GetDiskFreeSpaceExW(win32::Utf16(path).c_str(), &available, nullptr, nullptr)) return 0.0;
    return static_cast<double>(static_cast<std::int64_t>(available.QuadPart));
}

std::string VolumeLabel(const char* root) {
    char label[MAX_PATH + 1] = {};
    if (!gameapi::GetVolumeInformationA(root, label, sizeof(label), nullptr, nullptr, nullptr, nullptr, 0)) return {};
    return label;
}

} // namespace platform
