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

    // The key names of `section` (the part before '=', trimmed) into `out` (emptied
    // first). Returns 0 for an empty or unreadable section.
    bool GetKeyNames(const char* section, std::vector<std::string>* out);

    // The value (at most 1023 characters), or `defaultValue`.
    std::string GetString(const char* section, const char* key, const char* defaultValue);
    // Appends the items of the value split at `separator` (trimmed) to `out`. Returns 0
    // when the value is empty.
    bool GetList(const char* section, const char* key, std::vector<std::string>* out, char separator);

    bool GetIntArray(const char* section, const char* key, std::vector<std::uint32_t>* out);

    std::string path_;
};
