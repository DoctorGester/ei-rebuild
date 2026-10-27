#pragma once

// Text in UTF-8 (docs/notes/standalone.md section 4, tickets U1-U5). The game's assets hold their
// display text in one 8-bit code page (Windows-1251 for the Russian data); with EI_UTF8 every string
// inside the game is UTF-8, and asset text is converted where it is read:
//   - the text database (texts.res: TextSources::Find, text_sources.cpp)
//   - .reg string values (CFileRegistry's string readers, registry.cpp: menus.reg, quest.reg, smessbase.reg)
//   - .mob strings (Chunk::ReadString / ReadEncodedString, chunk_stream.cpp: unit names, dialogue, scripts)
//   - the titles' script credits.scr (CCreditsScreen::Build, ui_layouts.cpp)
// Identifiers and file names inside assets are ASCII, which is the same in both. RTF (the titles'
// pages) names its own code page and is left as it is.
//
// EI_UTF8 is a compile-time switch (`build.cmd verify utf8`, CMake -DEI_UTF8=ON), off by default; with it
// off nothing here changes a byte. EI_ASSET_CODEPAGE (CMake cache, default 1251) is the assets' code page.

#include "lib/mfc.h"

#include <string>
#include <string_view>

#ifndef EI_UTF8
#define EI_UTF8 0
#endif
#ifndef EI_ASSET_CODEPAGE
#define EI_ASSET_CODEPAGE 1251
#endif

namespace text {

inline constexpr bool kUtf8 = EI_UTF8 != 0;
// The code page the assets' text is written in.
inline constexpr unsigned kAssetCodePage = EI_ASSET_CODEPAGE;

// `bytes`, text in `codepage`, as UTF-8 (EI_UTF8), else unchanged.
std::string FromAssetText(std::string_view bytes, unsigned codepage = kAssetCodePage);
// A string just read from an asset, converted in place. Nothing happens without EI_UTF8.
void FromAssetText(mfc::CString* text, unsigned codepage = kAssetCodePage);
void FromAssetText(std::string* text, unsigned codepage = kAssetCodePage);
// The other way, for looking up an asset by a name that is text (the text database's keys, one of which
// holds Cyrillic): with EI_UTF8 and `text` UTF-8 that is not plain ASCII, `*out` becomes it in `codepage`
// and the result is true; otherwise false (`text` is the asset's bytes as it is).
bool ToAssetText(std::string_view text, std::string* out, unsigned codepage = kAssetCodePage);

// The length the original measured `text` by where a length means time (a briefing phrase lasts 0.05 s
// a character) or a limit (a typed line's): its bytes in the asset code page. With EI_UTF8 a character's
// UTF-8 sequence counts once; a colour escape ("\007C" and three raw bytes, built by the game, never in
// the assets or typed) counts its 5 bytes.
int CharacterCount(std::string_view text);
int CharacterCount(const mfc::CString& text);
inline int CharacterCount(const std::string& text) { return CharacterCount(std::string_view(text)); }

// --- Lengths and cuts by character (U5) -----------------------------------------------------------
//
// Where the original limits or indexes text by bytes (one byte one character in the asset code page),
// with EI_UTF8 these count characters as CharacterCount does and never cut inside a UTF-8 sequence.
// Without EI_UTF8 each is the original's byte count.

// The bytes of the character at `p` (not its terminator): with EI_UTF8 its lead byte and the continuation
// bytes after it, else 1.
int CharacterLength(const char* p);
// The bytes of `text`'s first `characters` characters (all of it when it has fewer; 0 for none).
int CharacterBytes(std::string_view text, int characters);
int CharacterBytes(const mfc::CString& text, int characters);
inline int CharacterBytes(const std::string& text, int characters) {
    return CharacterBytes(std::string_view(text), characters);
}
// The longest start of `text` that fits `room` bytes (`room` without the terminator) and ends at a
// character's end: `text`'s size when it fits, else `room` without EI_UTF8.
std::size_t FittingBytes(std::string_view text, std::size_t room);
// `from` copied into `to` (`size` bytes, a terminator always written; nothing when `size` is 0), cut to fit
// at a character's end (strncpy_s with _TRUNCATE without EI_UTF8).
void CopyFitting(char* to, std::size_t size, const char* from);

// --- Typing (U4) ---------------------------------------------------------------------------------
//
// With EI_UTF8 the game window is a Unicode one (system_text.h), so a typed character reaches the editors
// (the message box's edit field, the console line, the chat line, the save window's name) as its code point
// (app.cpp's DispatchInput joins a surrogate pair) and is stored as UTF-8. The editors' caret stays a byte
// offset, always at a character's start: it moves, deletes and limits the length by character. Without
// EI_UTF8 a character is the byte the original took, and each step below is the original's one byte.

// `head` with typed character `character` after it: its UTF-8 bytes with EI_UTF8, else the byte
// (mfc::operator+(head, char), as the original joined them).
mfc::CString Typed(const mfc::CString& head, int character);
// The bytes typed character `character` takes: its UTF-8 length with EI_UTF8, else 1.
int TypedLength(int character);
// The caret's step right from byte offset `at` of `text`: the next character's start (`at` + 1 at or past
// either end, as the original stepped before clamping).
int NextCharacter(const mfc::CString& text, int at);
// The caret's step left: the previous character's start (`at` - 1 at or past either end).
int PreviousCharacter(const mfc::CString& text, int at);
// The start of the character holding byte offset `at` (`at` itself without EI_UTF8 or outside the text).
int CharacterStart(const mfc::CString& text, int at);

} // namespace text
