// The console window's text and placement. See docs/notes/console.md.
#include "console.h"
#include "ui.h"

#include "text_split.h"

namespace console {

// The rectangle in pixels, clamped to at least 76x69 and at most the screen; the number
// of 15-pixel lines that fit below the 24-pixel margin, less one; the clamped rectangle back in
// 800x600 coordinates. A shown window gets a new surface.
void ConsoleWindow::SetRect(const Rect* newRect) {
    int screenWidth = screen->width;
    int screenHeight = screen->height;
    screenRect = *newRect;
    // The 3D interface scales 800x600 to pixels (Rebirth patches it, so it is called through its address).
    screen->ScaleRectToScreenEntry(&screenRect);
    width = screenRect.right - screenRect.left;
    height = screenRect.bottom - screenRect.top;
    if (width < 0x4C) width = 0x4C;
    if (width > screenWidth) width = screenWidth;
    if (height < 0x45) height = 0x45;
    if (height > screenHeight) height = screenHeight;
    visibleLines = (height - 0x18) / 15 - 1;
    screenRect.right = screenRect.left + width;
    screenRect.bottom = screenRect.top + height;
    rect = screenRect;
    screen->ScaleRectFromScreenEntry(&rect);
    if (shown) {
        Show(false);
        Show(true);
    }
}

void ConsoleWindow::SetHistory(int newLines) {
    history = newLines;
    DropOldLines(newLines);
    DrawLines();   // clamps the scroll position, draws the visible lines
}

void ConsoleWindow::DropOldLines(int count) {
    if (static_cast<int>(lines.size()) > count) lines.erase(lines.begin(), lines.end() - count);
}

// The first piece continues the last line, each '\n' starts a new one. A window scrolled
// back stays on the same lines. Keeps the last history lines.
void ConsoleWindow::AddText(std::string_view text) {
    std::vector<std::string> pieces = text::Split(text, '\n');   // at least one piece
    if (!lines.empty())
        lines.back() += pieces[0];
    else
        lines.push_back(pieces[0]);
    lines.insert(lines.end(), pieces.begin() + 1, pieces.end());
    if (scroll != 0) scroll += static_cast<int>(pieces.size());
    DropOldLines(history);
    DrawLines();   // clamps the scroll position, draws the visible lines
}

} // namespace console
