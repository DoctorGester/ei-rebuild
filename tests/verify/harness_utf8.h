#pragma once

// The UTF-8 build's file names (EI_UTF8, ticket U2; verify build only, harness_utf8.cpp): at harness start a
// folder and a file with Cyrillic names are made, listed, read back and removed through the game's own file
// calls (game.exe's import slots: the image's UTF-8 versions under the harness's saves redirect, the calls
// CFile, CFileFind and the CRT make), and the name on disk is checked as UTF-16. Without EI_UTF8 nothing runs.

#include <string>

namespace ei::verify::utf8files {

// The check, under the game's saves folder (the harness's saves redirect takes it to the run's overlay):
// empty when the names round-trip, else what failed.
std::string Check(const char* savesDir);

} // namespace ei::verify::utf8files
