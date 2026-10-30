#pragma once

// Where the program runs from (docs/notes/platform.md). Paths are UTF-8, with backslashes between folders as the game
// writes them.

#include <cstdint>
#include <string>

namespace platform {

// The full path of the running program's file.
std::string ExePath();
// The process's current folder, without a trailing separator but at a drive's root ("C:\").
std::string CurrentFolder();

// For the standalone's own files, which never went through the game's slots (the settings file; a harness run leaves
// them alone): the same, asked of the system directly.
// The full path of the running program's file.
std::string ProgramPath();
// The folder for the current user's program data, with a trailing separator; empty when the user has none.
std::string UserDataFolder();
// Makes `path` the process's current folder; false when it could not, with the system's error code in `error`.
bool SetCurrentFolder(const std::string& path, std::uint32_t* error);

} // namespace platform
