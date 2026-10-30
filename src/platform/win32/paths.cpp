// Where the program runs from, on Windows (platform/paths.h). ExePath and CurrentFolder go through game.exe's import
// slots, which the image routes to the UTF-8 calls; ProgramPath and UserDataFolder call the system directly.
#include "platform/paths.h"

#include "lib/gameapi.h"
#include "platform/win32/utf16.h"

#include <utility>

namespace platform {

namespace {

// Room for any path the calls return (the long-path limit, in UTF-8 bytes).
constexpr DWORD kPathBufferSize = 0x8000;

// `path` cut to the length a call returned: none when the call failed or the path did not fit.
std::string Fitted(std::string path, DWORD length) {
    path.resize(length < kPathBufferSize ? length : 0);
    return path;
}

} // namespace

std::string ExePath() {
    std::string path(kPathBufferSize, '\0');
    const DWORD length = gameapi::GetModuleFileNameA(nullptr, path.data(), kPathBufferSize);
    return Fitted(std::move(path), length);
}

std::string CurrentFolder() {
    std::string folder(kPathBufferSize, '\0');
    const DWORD length = gameapi::GetCurrentDirectoryA(kPathBufferSize, folder.data());
    return Fitted(std::move(folder), length);
}

std::string ProgramPath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) {
            path.resize(length);
            break;
        }
        path.resize(path.size() * 2);
    }
    return win32::Utf8(path);
}

bool SetCurrentFolder(const std::string& path, std::uint32_t* error) {
    if (SetCurrentDirectoryW(win32::Utf16(path).c_str())) return true;
    *error = GetLastError();
    return false;
}

std::string UserDataFolder() {
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (size == 0) return {};
    std::wstring folder(size, L'\0');
    folder.resize(GetEnvironmentVariableW(L"LOCALAPPDATA", folder.data(), size));
    if (folder.empty()) return {};
    if (folder.back() != L'\\') folder += L'\\';
    return win32::Utf8(folder);
}

} // namespace platform
