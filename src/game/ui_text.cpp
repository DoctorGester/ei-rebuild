// Text layout on interface surfaces (game.exe 005FDFA0). See docs/notes/ui.md ("Text layout").
#include "ui.h"
#include "console.h"

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
struct TextMetricsAt {
    SIZE TextExtentRaw(char* text, int font);
    // Passes `text` by value: its data goes to the callee, which releases it.
    SIZE TextExtent(mfc::CString text, int font) {
        char* data;
        std::memcpy(&data, &text, sizeof(data));
        mfc::CString empty;
        std::memcpy(&text, &empty, sizeof(data));   // `text` now owns nothing
        return TextExtentRaw(data, font);
    }
};
__declspec(naked) SIZE TextMetricsAt::TextExtentRaw(char*, int) EI_ORIGINAL(0x005F8340)

// 005FDE20 (Rebirth-patched): draws one line into the layout rectangle.
struct SurfaceTextAt {
    int Text(const char* text, RECT* rect, int font, COLORREF colour, int format, int flag);
};
int SurfaceTextAt::Text(const char* text, RECT* rect, int format, COLORREF colour, int a, int b) { return SurfaceDrawText(this, nullptr, text, rect, format, colour, a, b); }

// The character at `p` as the original compares it: a lead byte shifted left by 8 (sign-extended)
// plus the next byte (sign-extended), otherwise the byte itself.
int CharAt(const char* p) {
    int value = 0;
    const char* q = p;
    if (IsMbbLead(static_cast<unsigned char>(*p))) {
        value = static_cast<signed char>(*p) << 8;
        q = p + 1;
    }
    return value + static_cast<signed char>(*q);
}

constexpr std::uintptr_t kNewline = 0x00784340;   // "\n" appended to flush the last line
constexpr std::uintptr_t kSpace = 0x0078DA40;     // " " put back after each word

} // namespace

int CUiSurface::DrawWrappedText(const char* text, const RECT* rect, int maxLines, int font, COLORREF colour,
                                int format, int flag) {
    void* inner = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(this) + 0x10);
    if (!inner) return 0;
    TextMetricsAt* metrics = reinterpret_cast<TextMetricsAt*>(interface_);
    int width = rect->right - rect->left;
    mfc::CString ch, word, line;
    mfc::CString source(text);
    source += console::GameText(kNewline);
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
            if (metrics->TextExtent(line + word, font).cx > width) {
                lines.SetAtGrow(lines.GetSize(), line);
                lines.SetAtGrow(lines.GetSize(), word);
            } else {
                line += word;
                lines.SetAtGrow(lines.GetSize(), line);
            }
            line.Empty();
            word.Empty();
        } else if (c == ' ') {
            if (metrics->TextExtent(line + word, font).cx > width) {
                lines.SetAtGrow(lines.GetSize(), line);
                line = word + console::GameText(kSpace);
            } else {
                line += word;
                line += console::GameText(kSpace);
            }
            word.Empty();
        } else {
            word += ch;
        }
    }
    RECT at = *rect;
    int height = rect->bottom - rect->top;
    for (int i = 0; i < maxLines && i < lines.GetSize(); ++i) {
        reinterpret_cast<SurfaceTextAt*>(this)->Text(lines[i], &at, font, colour, format, flag);
        gameapi::OffsetRect(&at, 0, height);
    }
    return lines.GetSize();
}
EI_REPLACE_METHOD(0x005FDFA0, CUiSurface::DrawWrappedText);
