// The platform's paths on Windows (platform.h, "Paths"): the program's file and folder, the current folder, the user's
// data folder and the command line, through the wide calls.
#include "platform/platform.h"

#include "platform/win32/platform_win32.h"

#include <string>

namespace platform {

namespace {

// The program's file's folder, without a trailing separator.
std::string ProgramFolder() {
    std::string folder = ProgramPath();
    const std::size_t slash = folder.find_last_of("\\/");
    folder.resize(slash == std::string::npos ? 0 : slash);
    return folder;
}

} // namespace

std::string CurrentFolder() {
    const DWORD needed = GetCurrentDirectoryW(0, nullptr);
    std::wstring folder(needed, L'\0');
    const DWORD length = needed ? GetCurrentDirectoryW(needed, folder.data()) : 0;
    folder.resize(length < needed ? length : 0);
    return text::Narrow(folder);
}

std::string ArgumentsAfterSeparator(int, char**) {
    const wchar_t* p = ::GetCommandLineW();
    while (*p) {
        while (*p == L' ' || *p == L'\t') ++p;
        const wchar_t* token = p;
        bool quoted = false;
        for (; *p && (quoted || (*p != L' ' && *p != L'\t')); ++p)
            if (*p == L'"') quoted = !quoted;
        if (p - token == 2 && token[0] == L'-' && token[1] == L'-') {
            while (*p == L' ' || *p == L'\t') ++p;
            return text::Narrow(p);
        }
    }
    return {};
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
    return text::Narrow(path);
}

std::string DefaultGameFolder() { return ProgramFolder(); }

std::string DefaultOutputFolder() { return ProgramFolder(); }

std::string DefaultSettingsFolder() {
    const std::string folder = ProgramFolder();
    return folder.empty() ? folder : folder + "\\";
}

bool SetCurrentFolder(const std::string& path, std::uint32_t* error) {
    if (SetCurrentDirectoryW(text::Wide(path).c_str())) return true;
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
    return text::Narrow(folder);
}

} // namespace platform
