#pragma once

// CIniFile (provisional name): the game's .ini settings reader and
// writer over GetPrivateProfile* / WritePrivateProfileString. The object is just the file path.
// Lists are comma-separated values ("1, 2, 3"), each item trimmed.

#include <cstdint>
#include <string>
#include <vector>

#include "registry.h"

class CIniFile {
public:
    void SetPath(std::string_view path);

    // The key names of `section` (the part before '=', trimmed); none for an empty or unreadable section.
    std::vector<std::string> GetKeyNames(const char* section);

    // The value (at most 1023 characters), or `defaultValue`.
    std::string GetString(const char* section, const char* key, const char* defaultValue);
    // The items of the value split at `separator` (trimmed); none when the value is empty.
    std::vector<std::string> GetList(const char* section, const char* key, char separator);

    // The comma-separated list's items as integers; none when the value is empty.
    std::vector<std::uint32_t> GetIntArray(const char* section, const char* key);

    std::string path;
};
