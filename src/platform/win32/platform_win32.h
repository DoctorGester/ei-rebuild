#pragma once

// The Windows backend's own declarations (platform_win32.cpp, game_calls.cpp), for its files and the harness.
//
// Text at the system boundary in UTF-8 (docs/notes/standalone.md section 4). Every string inside the game is UTF-8,
// so the backend calls the wide (UTF-16) Windows functions, converting with Wide and Narrow at the call. Text going
// in that is not valid UTF-8 is taken in the ANSI code page, as the ANSI calls took it (8-bit text that has not
// crossed text::FromAssetText, game/asset_text.h). Text coming out is UTF-8.

#include <windows.h>

#include <string>
#include <string_view>

struct fd_set;
struct timeval;

namespace text {

// `text` (UTF-8, or else ANSI-code-page bytes) as UTF-16.
std::wstring Wide(std::string_view text);
// UTF-16 as UTF-8.
std::string Narrow(std::wstring_view wide);

} // namespace text

// The system calls of platform_win32.cpp's files and socket waits that a harness run answers itself: game_calls.cpp's,
// which the harness's builds replace with their own (docs/notes/platform.md, "The harness's answers").
namespace platform::win32 {

// WriteFile's call for File::Write, synchronous; `path` is the file as the game named it (the harness watches the
// game's writes: tests/verify/harness_crash.h).
BOOL WriteFile(HANDLE file, const void* data, DWORD size, DWORD* written, std::string_view path);
// select's call (the count is ignored by Winsock).
int Select(fd_set* read, fd_set* write, fd_set* failed, const timeval* timeout);

} // namespace platform::win32
