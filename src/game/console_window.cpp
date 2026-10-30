// The console window's text and placement. See docs/notes/console.md.
#include "console.h"

#include "text_split.h"
#include "ui.h"

#include <new>

namespace console {

// The rectangle in pixels, clamped to at least 76x69 and at most the screen; the number
// of 15-pixel lines that fit below the 24-pixel margin, less one; the clamped rectangle back in
// 800x600 coordinates. A shown window gets a new surface.
void ConsoleWindow::SetRect(const Rect* newRect) {
    int screenWidth = screen->width;
    int screenHeight = screen->height;
    screenRect = *newRect;
    // The 3D interface scales 800x600 to pixels (Rebirth patches it, so it is called through its address).
    screen->ScaleRectToScreen(&screenRect);
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
    screen->ScaleRectFromScreen(&rect);
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

// --- Create ------------------------------------------------------------------------------------------

// The console window's constructor. See console.h and docs/notes/console.md.

namespace console {

namespace {
constexpr int kFontSize = 90;            // tenths of a point
constexpr const char* kFontFace = "Courier New";
constexpr Rect kInitialRect = {0, 0, 800, 150};
constexpr int kHistory = 0x100;
constexpr int kNoCommand = -1;
} // namespace

ConsoleWindow::ConsoleWindow(Interface3D* newScreen) {
    screen = newScreen;
    surface = nullptr;
    commandIndex = kNoCommand;
    caret = 0;
    anchor = 0;
    scroll = 0;
    shown = false;
    ctrl = false;
    shift = false;
    font = PointFont(kFontSize, kFontFace);
    Rect area = kInitialRect;
    SetRect(&area);
    history = kHistory;
    DropOldLines(kHistory);
    DrawLines();
}

} // namespace console
