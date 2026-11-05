// Text layout on interface surfaces. See docs/notes/ui.md ("Interface base").
#include "ui.h"

#include "asset_text.h"
#include "console.h"
#include "screen_layout.h"
#include "platform/log.h"
#include "lib/rect.h"
#include "render/render_text.h"

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

std::vector<std::string> Interface3D::WrapText(std::string_view text, int maxWidth, int font) const {
    std::string ch, word, line;
    std::string source(text);
    source += "\n";
    std::vector<std::string> lines;
    // Words collect characters up to a space or newline; a word that would make the line wider
    // than the width starts a new line (a newline then also ends the word's line).
    for (const char* p = source.c_str(); *p != 0; ++p) {
        const char c = *p;
        ch.assign(1, c);
        if (c == '\n') {
            if (TextExtent(line + word, font).w > maxWidth) {
                lines.push_back(line);
                lines.push_back(word);
            } else {
                line += word;
                lines.push_back(line);
            }
            line.clear();
            word.clear();
        } else if (c == ' ') {
            if (TextExtent(line + word, font).w > maxWidth) {
                lines.push_back(line);
                line = word + " ";
            } else {
                line += word;
                line += " ";
            }
            word.clear();
        } else {
            word += ch;
        }
    }
    return lines;
}

int UiSurface::DrawWrappedText(std::string_view text, const Rect& area, int maxLines, const TextStyle& style) {
    const std::vector<std::string> lines = g_interface.WrapText(text, area.Width(), style.font);
    Rect at = area;
    int boxHeight = area.Height();
    const int count = static_cast<int>(lines.size());
    for (int i = 0; i < maxLines && i < count; ++i) {
        DrawTextIn(lines[i], at, style);
        at.Offset(0, boxHeight);
    }
    return count;
}

// --- TextExtent ---------------------------------------------------------------------------------------

Size Interface3D::TextExtent(std::string_view text, int font) const {
    const Size size = ::TextExtent(fonts[font], text);
    return screen_layout::TextExtentToLayout(size);
}
