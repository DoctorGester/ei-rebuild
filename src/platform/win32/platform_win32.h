#pragma once

// The Windows backend's own declarations (platform_win32.cpp, game_calls.cpp), for its files and the harness's Windows
// parts (tests/verify/system/*_win32.cpp).
//
// Text at the system boundary in UTF-8 (docs/notes/standalone.md section 4). Every string inside the game is UTF-8,
// so the backend calls the wide (UTF-16) Windows functions, converting with Wide and Narrow at the call. Text going
// in that is not valid UTF-8 is taken in the ANSI code page, as the ANSI calls took it (8-bit text that has not
// crossed text::FromAssetText, game/asset_text.h). Text coming out is UTF-8.

#include <windows.h>

#include <string>
#include <string_view>

namespace text {

// `text` (UTF-8, or else ANSI-code-page bytes) as UTF-16.
std::wstring Wide(std::string_view text);
// UTF-16 as UTF-8.
std::string Narrow(std::wstring_view wide);

} // namespace text

namespace platform::win32 {

// Another program's window `other` brought to the front, restored from the task bar, unless it is in front already;
// false for none (null).
bool BringForward(HWND other);

} // namespace platform::win32
