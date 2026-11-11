#pragma once

// The Windows backend's own declarations (src/platform/win32/*.cpp, one file per area of platform.h), for its files and
// the harness's Windows parts (tests/verify/system/*_win32.cpp).
//
// Text at the system boundary in UTF-8 (docs/notes/standalone.md section 4). Every string inside the game is UTF-8,
// so the backend calls the wide (UTF-16) Windows functions, converting with Wide and Narrow at the call. Text going
// in that is not valid UTF-8 is taken in the ANSI code page, as the ANSI calls took it (8-bit text that has not
// crossed text::FromAssetText, engine/core/asset_text.h). Text coming out is UTF-8.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN   // windows.h without its rarely used parts (shell, OLE, ...): 1.8 MB less to preprocess
#endif
#include <windows.h>

#include <string>
#include <string_view>

namespace text {

// `text` (UTF-8, or else ANSI-code-page bytes) as UTF-16.
std::wstring Wide(std::string_view text);
// UTF-16 as UTF-8.
std::string Narrow(std::wstring_view wide);

} // namespace text
