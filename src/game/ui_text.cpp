// Text layout on interface surfaces. See docs/notes/ui.md ("Interface base").
#include "ui.h"
#include "game_strings.h"
#include "console.h"

#include "lib/gameapi.h"
#include <string>

#include <cstring>
#include <string_view>
#include <vector>

std::vector<std::string> CInterface3D::WrapText(std::string_view text, int maxWidth, int font) const {
    std::string ch, word, line;
    std::string source(text);
    source += game::NewLineText();
    std::vector<std::string> lines;
    // Words collect characters up to a space or newline; a word that would make the line wider
    // than the width starts a new line (a newline then also ends the word's line).
    for (const char* p = source.c_str(); *p != 0; ++p) {
        const char c = *p;
        ch.assign(1, c);
        if (c == '\n') {
            if (TextExtent(line + word, font).cx > maxWidth) {
                lines.push_back(line);
                lines.push_back(word);
            } else {
                line += word;
                lines.push_back(line);
            }
            line.clear();
            word.clear();
        } else if (c == ' ') {
            if (TextExtent(line + word, font).cx > maxWidth) {
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
    return lines;
}

int CUiSurface::DrawWrappedText(std::string_view text, const RECT* area, int maxLines, int font, COLORREF colour,
                                UINT format, bool shadow) {
    if (!holder) return 0;
    const std::vector<std::string> lines = ui->WrapText(text, area->right - area->left, font);
    RECT at = *area;
    int boxHeight = area->bottom - area->top;
    const int count = static_cast<int>(lines.size());
    for (int i = 0; i < maxLines && i < count; ++i) {
        DrawTextIn(lines[i], &at, font, colour, format, shadow);
        gameapi::OffsetRect(&at, 0, boxHeight);
    }
    return count;
}
