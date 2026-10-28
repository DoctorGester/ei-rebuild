#pragma once

// Opening files through the game's import slots (CreateFileA, GetFileAttributesA): a mod's hook in the
// import table and the harness's save / install redirection apply to every file the game opens. What is
// done with an open file is the host CRT's (std::fread, std::fseek, ...) or Win32's.

#include <cstdio>

namespace lib {

// fopen: `mode` is r, w or a, then optionally + and b or t (text when neither). The file is opened by
// CreateFileA through its slot, shared for reading and writing, then becomes a host stream; null when
// either fails.
std::FILE* OpenFile(const char* path, const char* mode);

// Whether `path` names a file or folder (GetFileAttributesA through its slot; the original's _access
// with mode 0 or 4).
bool FileExists(const char* path);

} // namespace lib
