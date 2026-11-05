// The console window's text and placement. See docs/notes/console.md.
#include "console.h"

#include "screen_layout.h"
#include "text_split.h"
#include "ui.h"

#include "platform/log.h"

#include <cmath>
#include <new>

namespace console {

namespace {
constexpr int kFontSize = 90;            // tenths of a point at the 800x600 layout's size
constexpr const char* kFontFace = "Courier New";
constexpr int kMinWidth = 0x4C;          // layout pixels, like every size the window draws with
constexpr int kMinHeight = 0x45;
constexpr int kTopMargin = 0x18;
constexpr int kLineHeight = 15;
constexpr int kLayoutHeight = 600;
} // namespace

int ConsoleWindow::Px(int layoutPixels) const {
    return static_cast<int>(std::lround(static_cast<float>(layoutPixels) * scale));
}

// The text and the metrics follow the window like the rest of the interface: one pixel of the 800x600 layout is
// height43 / 600 screen pixels (the window's 4:3 part), whatever the system's DPI or display scale (a Retina window
// is as many pixels as it has physical ones).
void ConsoleWindow::SetScale() {
    scale = static_cast<float>(screen_layout::Get().height43) / kLayoutHeight;
    font = PointFont(Px(kFontSize), kFontFace);
}

// The rectangle in pixels, clamped to at least 76x69 layout pixels (scaled) and at most the screen; the number of lines
// that fit below the 24-pixel margin, less one; the clamped rectangle back in 800x600 coordinates. A shown window gets
// a new surface. The requested rectangle is kept, so a resolution change places it again (OnResize).
void ConsoleWindow::SetRect(const Rect& newRect) {
    int screenWidth = g_interface.width;
    int screenHeight = g_interface.height;
    requestedRect = newRect;
    SetScale();
    // The 3D interface's 800x600 layout to pixels.
    screenRect = screen_layout::ToScreen(newRect);
    width = screenRect.Width();
    height = screenRect.Height();
    if (width < Px(kMinWidth)) width = Px(kMinWidth);
    if (width > screenWidth) width = screenWidth;
    if (height < Px(kMinHeight)) height = Px(kMinHeight);
    if (height > screenHeight) height = screenHeight;
    visibleLines = (height - Px(kTopMargin)) / Px(kLineHeight) - 1;
    ei::Log("Console window: %dx%d pixels, font %d px, lines %d px, %d visible", width, height, font.pixels,
            Px(kLineHeight), visibleLines);
    screenRect.right = screenRect.left + width;
    screenRect.bottom = screenRect.top + height;
    rect = screen_layout::FromScreen(screenRect);
    if (shown) {
        Show(false);
        Show(true);
    }
}

void ConsoleWindow::OnResize() { SetRect(requestedRect); }

void ConsoleWindow::SetHistory(int newLines) {
    history = newLines;
    DropOldLines(newLines);
    ClampScroll();
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
    ClampScroll();
}

} // namespace console

// --- Create ------------------------------------------------------------------------------------------

// The console window's constructor. See console.h and docs/notes/console.md.

namespace console {

namespace {
constexpr Rect kInitialRect = {0, 0, 800, 150};
} // namespace

ConsoleWindow::ConsoleWindow() {
    Rect area = kInitialRect;
    SetRect(area);
    DropOldLines(kHistory);
    ClampScroll();
}

} // namespace console
