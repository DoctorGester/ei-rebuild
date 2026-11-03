#pragma once

// IniFile: the game's .ini files read (config\movie.ini, the lights tables config\Lights*.ini, Rebirth's
// EI_Plugin.ini), on every system, with the rules of the Windows profile reads the original made: sections and keys
// matched without case, the first match counts, a line starting with ';' and a line without '=' skipped, a value
// trimmed and freed of one pair of matching quotes. The file is read when the path is set (platform::ReadFile) and
// taken from the asset code page into UTF-8; a missing file reads as empty. Lists are comma-separated values
// ("1, 2, 3"), each item trimmed.

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class IniFile {
public:
    void SetPath(std::string_view path);

    // The key names of `section` (the part before '=', trimmed), in file order; none for an empty or missing section.
    std::vector<std::string> GetKeyNames(const char* section) const;

    // The value, or `defaultValue`.
    std::string GetString(const char* section, const char* key, const char* defaultValue) const;
    // The value's leading decimal number, or `defaultValue` when the key is missing or its value starts with none.
    int GetInt(const char* section, const char* key, int defaultValue) const;
    // The items of the value split at `separator` (trimmed); none when the value is empty.
    std::vector<std::string> GetList(const char* section, const char* key, char separator) const;

    // The comma-separated list's items as integers; none when the value is empty.
    std::vector<std::uint32_t> GetIntArray(const char* section, const char* key) const;

private:
    // The entries of `section` ("key=value" lines, trimmed), in file order.
    std::vector<std::string_view> Entries(std::string_view section) const;

    std::string text;   // the file's, UTF-8
};
