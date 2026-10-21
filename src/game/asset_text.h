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
// The other way, for looking up an asset by a name that is text (the text database's keys, one of which
// holds Cyrillic): with EI_UTF8 and `text` UTF-8 that is not plain ASCII, `*out` becomes it in `codepage`
// and the result is true; otherwise false (`text` is the asset's bytes as it is).
bool ToAssetText(std::string_view text, std::string* out, unsigned codepage = kAssetCodePage);
// In place (nothing happens without EI_UTF8). Until U4 also the editors' boundary: the save window's
// name is edited byte by byte, so it takes its title in 8 bits and gives it back through FromAssetText.
void ToAssetText(mfc::CString* text, unsigned codepage = kAssetCodePage);

// The length the original measured `text` by where a length means time (a briefing phrase lasts 0.05 s
// a character): its bytes in the asset code page. With EI_UTF8 a character's UTF-8 sequence counts once;
// a colour escape ("\007C" and three raw bytes, built by the game, never in the assets) counts its 5 bytes.
int CharacterCount(std::string_view text);

// --- The boundary until U3-U4 -------------------------------------------------------------------
//
// Text still leaves through GDI's ANSI text draw and extent; there the UTF-8 text goes back to the
// assets' code page, the bytes the original passed, so what is drawn does not change. U3 (drawing and
// measuring) replaces each use with the wide call and then removes this (the system calls are wide since
// U2: system_text.h). Text that is not valid UTF-8 passes unchanged: 8-bit text that has not crossed
// FromAssetText yet (typed characters before U4).
class ForAnsiCall {
public:
    // `length` -1: up to the terminator (and -1 stays -1, as the ANSI calls take it).
    explicit ForAnsiCall(const char* text, int length = -1) : text_(text), length_(length) {
        if constexpr (kUtf8) Convert();
    }
    ForAnsiCall(const ForAnsiCall&) = delete;
    ForAnsiCall& operator=(const ForAnsiCall&) = delete;

    const char* data() const { return text_; }
    int length() const { return length_; }
    operator const char*() const { return text_; }

private:
    void Convert();

    const char* text_;
    int length_;
    std::string converted_;
};

} // namespace text
