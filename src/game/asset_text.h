#pragma once

// Text in UTF-8: every string inside the game is UTF-8. Text in the asset code page (EI_ASSET_CODEPAGE, 1251 by
// default) is converted where it crosses into the game: the assets where they are read, the saves and the network
// both ways (they stay in the asset code page, as the original's, so saves and vanilla peers read the same bytes).
// A colour escape ("\007C" and three raw colour bytes, built by the game) is kept as it is both ways. See
// docs/notes/standalone.md section 4.

#include "lib/mfc.h"

#include <string>
#include <string_view>

#ifndef EI_ASSET_CODEPAGE
#define EI_ASSET_CODEPAGE 1251
#endif

namespace text {

inline constexpr unsigned kAssetCodePage = EI_ASSET_CODEPAGE;

// `bytes`, text in `codepage`, as UTF-8.
std::string FromAssetText(std::string_view bytes, unsigned codepage = kAssetCodePage);
// A string just read from an asset, a save or the network, converted in place.
void FromAssetText(std::string* text, unsigned codepage = kAssetCodePage);
// The other way, for looking up an asset by a text name (one text database key holds Cyrillic): true with `*out`
// set when `text` is valid UTF-8 and not plain ASCII.
bool ToAssetText(std::string_view text, std::string* out, unsigned codepage = kAssetCodePage);
// `text` as the save files and the network carry it: in `codepage`, a character the code page has no byte for as
// '?' (never a look-alike); text that is not valid UTF-8 as it is.
std::string ToAssetText(std::string_view text, unsigned codepage = kAssetCodePage);

// The length the game measures `text` by where a length means time (a briefing phrase lasts 0.05 s a
// character) or a limit: its characters, what the original's bytes in the asset code page counted. A colour
// escape ("\007C" and three raw bytes, built by the game) counts its 5 bytes.
int CharacterCount(std::string_view text);
inline int CharacterCount(const std::string& text) { return CharacterCount(std::string_view(text)); }

// --- Lengths and cuts by character -----------------------------------------------------------
// These count characters and never cut inside a UTF-8 sequence.

// The bytes of the character at `p`.
int CharacterLength(const char* p);
// The bytes of `text`'s first `characters` characters (all of it when it has fewer; 0 for none).
int CharacterBytes(std::string_view text, int characters);
inline int CharacterBytes(const std::string& text, int characters) {
    return CharacterBytes(std::string_view(text), characters);
}
// The longest start of `text` that fits `room` bytes (without the terminator) and ends at a character's end.
std::size_t FittingBytes(std::string_view text, std::size_t room);
// `from` copied into `to` (`size` bytes, always terminated; nothing when `size` is 0), cut at a character's end.
void CopyFitting(char* to, std::size_t size, const char* from);

// --- Typing ---------------------------------------------------------------------------------
// The game window is a Unicode one: a typed character reaches the editors as its code point (DispatchInput joins a
// surrogate pair) and is stored as UTF-8. The caret stays a byte offset at a character's start.

// `head` with typed character `character` after it.
std::string Typed(const std::string& head, int character);
// The bytes typed character `character` takes: its UTF-8 length.
int TypedLength(int character);
// The caret's step right from byte offset `at` (`at` + 1 at or past either end, as the game stepped before
// clamping).
int NextCharacter(const std::string& text, int at);
// The caret's step left: the previous character's start (`at` - 1 at or past either end).
int PreviousCharacter(const std::string& text, int at);
// The start of the character holding byte offset `at` (`at` itself outside the text).
int CharacterStart(const std::string& text, int at);

} // namespace text
