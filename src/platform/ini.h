#pragma once

// The game's .ini files (docs/notes/platform.md): the system's profile reads. Paths and values are bytes as the file
// holds them (the config\*.ini files are the game's own and their names are ASCII).

#include <string>
#include <vector>

namespace platform {

// The entries ("key=value", in file order) of `section` in the file `path`; none when the section or file is missing.
// At most 0x5000 bytes of them are read.
std::vector<std::string> IniSectionEntries(const char* section, const char* path);
// The string of `key` in `section` of the file `path`, `defaultValue` when it is not there; at most 1023 bytes.
std::string IniString(const char* section, const char* key, const char* defaultValue, const char* path);

// The number `key` holds in `section` of the file `path`, `defaultValue` when it is not there or is no number; asked of
// the system directly (the standalone's start-up reads EI_Plugin.ini, beside the game's slots).
int IniInt(const char* section, const char* key, int defaultValue, const char* path);

} // namespace platform
