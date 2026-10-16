// Text layout on interface surfaces (game.exe 005FDFA0). See docs/notes/ui.md ("Interface base").
#include "ui.h"
#include "game_strings.h"
#include "console.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_ui.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace {

// The game's multibyte CRT.

// 005F8340 (Rebirth-patched, called through its address): the extent of `text` in interface
// font `font`. It takes a CString by value and releases it; a naked forwarder cannot declare a
// by-value CString, so the string's data pointer is handed over (TextExtent).
// Passes `text` by value: its data goes to the callee, which releases it.
SIZE TextExtent(CInterface3D* ui, mfc::CString text, int font) {
    char* data;
    std::memcpy(&data, &text, sizeof(data));
    mfc::CString empty;
    std::memcpy(&text, &empty, sizeof(data));   // `text` now owns nothing
    SIZE size;
    InterfaceTextExtentOriginal(ui, nullptr, &size, data, font);
    return size;
}

// 005FDE20 (Rebirth-patched; through its address): draws one line into the layout rectangle.
int DrawLine(CUiSurface* surface, const char* text, RECT* rect, int font, COLORREF colour, int format, int flag) {
    return SurfaceDrawText(surface, nullptr, text, rect, font, colour, format, flag);
}

// The character at `p` as the original compares it: a lead byte shifted left by 8 (sign-extended)
// plus the next byte (sign-extended), otherwise the byte itself.
int CharAt(const char* p) {
    int value = 0;
    const char* q = p;
    if (libcrt::Ismbblead(static_cast<unsigned char>(*p))) {
        value = static_cast<signed char>(*p) << 8;
        q = p + 1;
    }
    return value + static_cast<signed char>(*q);
}


} // namespace

int CUiSurface::DrawWrappedText(const char* text, const RECT* rect, int maxLines, int font, COLORREF colour,
                                int format, int flag) {
    if (!holder_) return 0;
    int width = rect->right - rect->left;
    mfc::CString ch, word, line;
    mfc::CString source(text);
    source += game::NewLineText();
    console::CStringArray lines;
    // Words collect characters up to a space or newline; a word that would make the line wider
    // than the rectangle starts a new line (a newline then also ends the word's line).
    for (const char* p = source; CharAt(p) != 0; p = MbsInc(p)) {
        if (IsLeadByte(static_cast<signed char>(*p))) {
            console::Ops(ch).AssignChar(p[0]);
            console::Ops(ch).AppendChar(p[1]);
        } else {
            console::Ops(ch).AssignChar(*p);
        }
        int c = CharAt(p);
        if (c == '\n') {
            if (TextExtent(interface_, line + word, font).cx > width) {
                lines.SetAtGrow(lines.GetSize(), line);
                lines.SetAtGrow(lines.GetSize(), word);
            } else {
                line += word;
                lines.SetAtGrow(lines.GetSize(), line);
            }
            line.Empty();
            word.Empty();
        } else if (c == ' ') {
            if (TextExtent(interface_, line + word, font).cx > width) {
                lines.SetAtGrow(lines.GetSize(), line);
                line = word + game::SpaceText();
            } else {
                line += word;
                line += game::SpaceText();
            }
            word.Empty();
        } else {
            word += ch;
        }
    }
    RECT at = *rect;
    int height = rect->bottom - rect->top;
    for (int i = 0; i < maxLines && i < lines.GetSize(); ++i) {
        DrawLine(this, lines[i], &at, font, colour, format, flag);
        gameapi::OffsetRect(&at, 0, height);
    }
    return lines.GetSize();
}
EI_REPLACE_METHOD(0x005FDFA0, CUiSurface::DrawWrappedText);
