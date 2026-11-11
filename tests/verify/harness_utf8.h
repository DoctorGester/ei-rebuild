#pragma once

// The image's UTF-8 checks (docs/notes/standalone.md section 4; tests/scenarios/batch_menus_heroes.txt, utf8_typing;
// game.exe's text was in the ANSI code page). The checks are the image's (image/harness_utf8_image.cpp).


#include <string>

// File names (ticket U2): "utf8files" makes, lists, reads back and removes a folder and a file with Cyrillic names
// through the game's own file calls (the platform's File, FileFind and folder calls, under the harness's saves
// redirect), and checks the name on disk as UTF-16.
namespace ei::verify::utf8files {

// The check, under the game's saves folder (the harness's saves redirect takes it to the run's overlay):
// empty when the names round-trip, else what failed.
std::string Check(const char* savesDir);

} // namespace ei::verify::utf8files

// Typing (ticket U4). Typed text reaches the game as UTF-8 (the platform's Text events).
namespace ei::verify::utf8typing {

// "typeunicode <text>": each character of <text> (UTF-8 in the scenario) handed to the game as a Text event.
// Empty, or what failed.
std::string Type(void* window, const std::string& typed);

// "consoleline <n> <text>": the console's input line holds <text> in UTF-8 (<text> may be empty) and its caret
// stands at the start of character <n> (from 0). Empty, or what differs.
std::string CheckConsoleLine(const std::string& argument);

} // namespace ei::verify::utf8typing

// The text audit (ticket U5).
namespace ei::verify::utf8text {

// "utf8text": case and classes without the locale (lib/text_case.h): the CRT's case-insensitive compares
// (_stricmp, _strnicmp, _memicmp, _mbsicmp) match Cyrillic in either case and tell apart letters whose
// UTF-8 bytes share a trail; str::MakeLower and text::UpperInPlace change Cyrillic case; tolower / isspace of a
// UTF-8 byte leave it alone; the network's player name filter (IsPrintableText 0047ADA0) passes a Cyrillic
// name and rejects a cut sequence and a control character. Empty, or what failed.
std::string Check();

// "chatwrap <text>": <text> added to the interface's chat overlay (ChatOverlay::AddLine 005E2A30) breaks into
// more than one line, each valid UTF-8 (a long word is broken between characters), which joined give <text>
// back; the lines are cleared afterwards. Empty, or what failed.
std::string CheckChatWrap(const std::string& text);

} // namespace ei::verify::utf8text
