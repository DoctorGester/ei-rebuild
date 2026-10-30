#pragma once

// Where the program runs from (docs/notes/platform.md). Paths are UTF-8, with backslashes between folders as the game
// writes them.

#include <string>

namespace platform {

// The full path of the running program's file.
std::string ExePath();
// The process's current folder, without a trailing separator but at a drive's root ("C:\").
std::string CurrentFolder();

} // namespace platform
