// The .ini reads on Windows (platform/ini.h): the profile calls through game.exe's import slots.
#include "platform/ini.h"

#include "lib/gameapi.h"

#include <windows.h>

namespace platform {

std::vector<std::string> IniSectionEntries(const char* section, const char* path) {
    std::vector<std::string> entries;
    char buffer[0x5000];
    gameapi::GetPrivateProfileSectionA(section, buffer, sizeof(buffer), path);
    // Each entry ends with a zero byte, the last one with a second.
    const char* start = buffer;
    while (start < buffer + sizeof(buffer) && *start != '\0') {
        entries.emplace_back(start);
        start += entries.back().size() + 1;
    }
    return entries;
}

std::string IniString(const char* section, const char* key, const char* defaultValue, const char* path) {
    char buffer[0x400] = {};
    gameapi::GetPrivateProfileStringA(section, key, defaultValue, buffer, 0x3FF, path);
    return std::string(buffer);
}

int IniInt(const char* section, const char* key, int defaultValue, const char* path) {
    return static_cast<int>(GetPrivateProfileIntA(section, key, static_cast<INT>(defaultValue), path));
}

} // namespace platform
