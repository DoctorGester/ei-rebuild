#pragma once

// Text in UTF-8: with EI_UTF8 every string inside the game is UTF-8 and asset text (code page EI_ASSET_CODEPAGE,
// 1251 by default) is converted where it is read. Off by default; off changes no byte. See
// docs/notes/standalone.md section 4.

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
inline constexpr unsigned kAssetCodePage = EI_ASSET_CODEPAGE;

// `bytes`, text in `codepage`, as UTF-8 (EI_UTF8), else unchanged.
std::string FromAssetText(std::string_view bytes, unsigned codepage = kAssetCodePage);
// A string just read from an asset, converted in place. Nothing happens without EI_UTF8.
void FromAssetText(mfc::CString* text, unsigned codepage = kAssetCodePage);
void FromAssetText(std::string* text, unsigned codepage = kAssetCodePage);
// The other way, for looking up an asset by a text name (one text database key holds Cyrillic): true with `*out`
// set when EI_UTF8 is on and `text` is not plain ASCII.
bool ToAssetText(std::string_view text, std::string* out, unsigned codepage = kAssetCodePage);

// The length the game measures `text` by where a length means time (a briefing phrase lasts 0.05 s a
// character) or a limit: its bytes in the asset code page. A colour escape ("\007C" and three raw bytes, built
// by the game) counts its 5 bytes.
int CharacterCount(std::string_view text);
int CharacterCount(const mfc::CString& text);
inline int CharacterCount(const std::string& text) { return CharacterCount(std::string_view(text)); }

// --- Lengths and cuts by character -----------------------------------------------------------
// With EI_UTF8 these count characters and never cut inside a UTF-8 sequence; without it, bytes.

// The bytes of the character at `p`.
int CharacterLength(const char* p);
// The bytes of `text`'s first `characters` characters (all of it when it has fewer; 0 for none).
int CharacterBytes(std::string_view text, int characters);
int CharacterBytes(const mfc::CString& text, int characters);
inline int CharacterBytes(const std::string& text, int characters) {
    return CharacterBytes(std::string_view(text), characters);
}
// The longest start of `text` that fits `room` bytes (without the terminator) and ends at a character's end.
std::size_t FittingBytes(std::string_view text, std::size_t room);
// `from` copied into `to` (`size` bytes, always terminated; nothing when `size` is 0), cut at a character's end.
void CopyFitting(char* to, std::size_t size, const char* from);

// --- Typing ---------------------------------------------------------------------------------
// With EI_UTF8 the game window is a Unicode one: a typed character reaches the editors as its code point
// (DispatchInput joins a surrogate pair) and is stored as UTF-8. The caret stays a byte offset at a character's
// start.

// `head` with typed character `character` after it.
mfc::CString Typed(const std::string& head, int character);
// The bytes typed character `character` takes: its UTF-8 length with EI_UTF8, else 1.
int TypedLength(int character);
// The caret's step right from byte offset `at` (`at` + 1 at or past either end, as the game stepped before
// clamping).
int NextCharacter(const std::string& text, int at);
// The caret's step left: the previous character's start (`at` - 1 at or past either end).
int PreviousCharacter(const std::string& text, int at);
// The start of the character holding byte offset `at` (`at` itself without EI_UTF8 or outside the text).
int CharacterStart(const std::string& text, int at);

} // namespace text
