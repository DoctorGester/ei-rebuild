#pragma once

// The UTF-8 build's file names (EI_UTF8, ticket U2; verify build only, harness_utf8.cpp): at harness start a
// folder and a file with Cyrillic names are made, listed, read back and removed through the game's own file
// calls (game.exe's import slots: the image's UTF-8 versions under the harness's saves redirect, the calls
// CFile, CFileFind and the CRT make), and the name on disk is checked as UTF-16. Without EI_UTF8 nothing runs.

#include <windows.h>

#include <string>

namespace ei::verify::utf8files {

// The check, under the game's saves folder (the harness's saves redirect takes it to the run's overlay):
// empty when the names round-trip, else what failed.
std::string Check(const char* savesDir);

} // namespace ei::verify::utf8files

// Typing in the UTF-8 build (EI_UTF8, ticket U4; tests/scenarios/utf8_typing.txt). The game window is a Unicode
// one there, so a typed character reaches the game as UTF-16. Without EI_UTF8 both steps are skipped.
namespace ei::verify::utf8typing {

// "typeunicode <text>": each UTF-16 unit of <text> (UTF-8 in the scenario; a character past U+FFFF is two)
// posted to `window` as WM_CHAR with PostMessageW, as Windows gives a Unicode window a typed character. Empty,
// or what failed.
std::string Type(HWND window, const std::string& typed);

// "consoleline <n> <text>": the console's input line holds <text> in UTF-8 (<text> may be empty) and its caret
// stands at the start of character <n> (from 0). Empty, or what differs.
std::string CheckConsoleLine(const std::string& argument);

} // namespace ei::verify::utf8typing

// The UTF-8 build's text audit (EI_UTF8, ticket U5; tests/scenarios/utf8_typing.txt, its steps marked "utf8").
// Both need the standalone image (our code on our data).
namespace ei::verify::utf8text {

// "utf8text": case and classes without the locale (lib/text_case.h): the CRT's case-insensitive compares
// (_stricmp, _strnicmp, _memicmp, _mbsicmp) match Cyrillic in either case and tell apart letters whose
// UTF-8 bytes share a trail; CString::MakeLower and CharUpperA change Cyrillic case; tolower / isspace of a
// UTF-8 byte leave it alone; the network's player name filter (IsPrintableText 0047ADA0) passes a Cyrillic
// name and rejects a cut sequence and a control character. Empty, or what failed.
std::string Check();

// "chatwrap <text>": <text> added to the interface's chat overlay (CChatOverlay::AddLine 005E2A30) breaks into
// more than one line, each valid UTF-8 (a long word is broken between characters), which joined give <text>
// back; the lines are cleared afterwards. Empty, or what failed.
std::string CheckChatWrap(const std::string& text);

} // namespace ei::verify::utf8text
