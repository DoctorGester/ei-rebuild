// Text layout on interface surfaces. See docs/notes/ui.md ("Interface base").
#include "ui.h"
#include "game_strings.h"
#include "console.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include "ei/hook.h"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace {

// The game's multibyte CRT.

// The extent of `text` in interface font `font` (CInterface3D::TextExtent).
SIZE TextExtent(CInterface3D* ui, std::string_view text, int font) {
    SIZE size;
    ui->TextExtent(&size, text, font);
    return size;
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
    std::string ch, word, line;
    std::string source = text;
    source += game::NewLineText();
    std::vector<std::string> lines;
    // Words collect characters up to a space or newline; a word that would make the line wider
    // than the rectangle starts a new line (a newline then also ends the word's line).
    for (const char* p = source.c_str(); CharAt(p) != 0; p = gamecrt::mbsinc(p)) {
        if (libcrt::Isleadbyte(static_cast<signed char>(*p))) {
            ch.assign(1, p[0]);
            ch += p[1];
        } else {
            ch.assign(1, *p);
        }
        int c = CharAt(p);
        if (c == '\n') {
            if (TextExtent(interface_, line + word, font).cx > width) {
                lines.push_back(line);
                lines.push_back(word);
            } else {
                line += word;
                lines.push_back(line);
            }
            line.clear();
            word.clear();
        } else if (c == ' ') {
            if (TextExtent(interface_, line + word, font).cx > width) {
                lines.push_back(line);
                line = word + game::SpaceText();
            } else {
                line += word;
                line += game::SpaceText();
            }
            word.clear();
        } else {
            word += ch;
        }
    }
    RECT at = *rect;
    int height = rect->bottom - rect->top;
    const int count = static_cast<int>(lines.size());
    for (int i = 0; i < maxLines && i < count; ++i) {
        DrawLine(lines[i], &at, font, colour, format, flag);   // through its address
        gameapi::OffsetRect(&at, 0, height);
    }
    return count;
}
EI_REWRITTEN(0x005FDFA0, CUiSurface::DrawWrappedText);
