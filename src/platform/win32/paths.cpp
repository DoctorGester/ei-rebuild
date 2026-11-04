// The platform's paths on Windows (platform.h, "Paths"): the program's file, the current folder, the user's data folder
// and the command line, through the wide calls.
#include "platform/platform.h"

#include "platform/win32/platform_win32.h"

#include <string>

namespace platform {

std::string CurrentFolder() {
    const DWORD needed = GetCurrentDirectoryW(0, nullptr);
    std::wstring folder(needed, L'\0');
    const DWORD length = needed ? GetCurrentDirectoryW(needed, folder.data()) : 0;
    folder.resize(length < needed ? length : 0);
    return text::Narrow(folder);
}

std::string ArgumentsAfterSeparator(int, char**) {
    const char* p = ::GetCommandLineA();
    while (*p) {
        while (*p == ' ' || *p == '\t') ++p;
        const char* token = p;
        bool quoted = false;
        for (; *p && (quoted || (*p != ' ' && *p != '\t')); ++p)
            if (*p == '"') quoted = !quoted;
        if (p - token == 2 && token[0] == '-' && token[1] == '-') {
            while (*p == ' ' || *p == '\t') ++p;
            return p;
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
