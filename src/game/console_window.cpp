// The console window's text and placement (game.exe 0x005E3E90-0x005E4170). See
// docs/notes/console.md.
#include "console.h"

#include "ei/hook.h"
#include "originals_library.h"

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace console {

namespace {

// The 3D interface (ConsoleWindow::screen_). Rebirth patches both scaling functions (UI scaling),
// so they are called through their original addresses.
struct ScreenAt {
    void ScaleRectToScreen(int* rect);     // 005F7CA0 (800x600 -> pixels)
    void ScaleRectFromScreen(int* rect);   // 005F7DA0 (pixels -> 800x600)
};
__declspec(naked) void ScreenAt::ScaleRectToScreen(int*) EI_ORIGINAL(0x005F7CA0)
__declspec(naked) void ScreenAt::ScaleRectFromScreen(int*) EI_ORIGINAL(0x005F7DA0)

struct WindowAt : ConsoleWindow {
    void Show(bool shown);   // 005E3FD0: creates (or releases) the surface and redraws
    void Redraw();           // 005E4800: clamps the scroll position, draws the visible lines
};
__declspec(naked) void WindowAt::Show(bool) EI_ORIGINAL(0x005E3FD0)
__declspec(naked) void WindowAt::Redraw() EI_ORIGINAL(0x005E4800)

// 004DBE40: splits `text` at `separator` into `out`.

WindowAt* At(ConsoleWindow* w) { return static_cast<WindowAt*>(w); }

} // namespace

// 005E3E90: the rectangle in pixels, clamped to at least 76x69 and at most the screen; the number
// of 15-pixel lines that fit below the 24-pixel margin, less one; the clamped rectangle back in
// 800x600 coordinates. A shown window gets a new surface.
void ConsoleWindow::SetRect(const int* rect) {
    int screenWidth = screen_[0];
    int screenHeight = screen_[1];
    screenRect_[0] = rect[0];
    screenRect_[1] = rect[1];
    screenRect_[2] = rect[2];
    screenRect_[3] = rect[3];
    reinterpret_cast<ScreenAt*>(screen_)->ScaleRectToScreen(screenRect_);
    width_ = screenRect_[2] - screenRect_[0];
    height_ = screenRect_[3] - screenRect_[1];
    if (width_ < 0x4C) width_ = 0x4C;
    if (width_ > screenWidth) width_ = screenWidth;
    if (height_ < 0x45) height_ = 0x45;
    if (height_ > screenHeight) height_ = screenHeight;
    visibleLines_ = (height_ - 0x18) / 15 - 1;
    screenRect_[2] = screenRect_[0] + width_;
    screenRect_[3] = screenRect_[1] + height_;
    rect_[0] = screenRect_[0];
    rect_[1] = screenRect_[1];
    rect_[2] = screenRect_[2];
    rect_[3] = screenRect_[3];
    reinterpret_cast<ScreenAt*>(screen_)->ScaleRectFromScreen(rect_);
    if (shown_) {
        At(this)->Show(false);
        At(this)->Show(true);
    }
}
EI_REPLACE_METHOD(0x005E3E90, ConsoleWindow::SetRect);

// 005E3F90: keeps the last `lines` lines.
void ConsoleWindow::SetHistory(int lines) {
    history_ = lines;
    if (lines_.GetSize() > lines) lines_.RemoveAt(0, lines_.GetSize() - lines);
    At(this)->Redraw();
}
EI_REPLACE_METHOD(0x005E3F90, ConsoleWindow::SetHistory);

// 005E4090: the first piece continues the last line, each '\n' starts a new one. A window scrolled
// back stays on the same lines. Keeps the last history_ lines.
void ConsoleWindow::AddText(const char* text) {
    CStringArray pieces;
    StringSplit(text, &pieces, '\n');
    if (lines_.GetSize() != 0)
        lines_[lines_.GetSize() - 1] += *pieces.data_;   // the original reads piece 0 unchecked
    else
        lines_.SetAtGrow(lines_.GetSize(), *pieces.data_);
    for (int i = 1; i < pieces.GetSize(); ++i) lines_.SetAtGrow(lines_.GetSize(), pieces[i]);
    if (scroll_ != 0) scroll_ += pieces.GetSize();
    if (lines_.GetSize() > history_) lines_.RemoveAt(0, lines_.GetSize() - history_);
    At(this)->Redraw();
}
EI_REPLACE_METHOD(0x005E4090, ConsoleWindow::AddText);

} // namespace console
