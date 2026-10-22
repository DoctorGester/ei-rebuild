// The console window's text and placement (game.exe 0x005E3E90-0x005E4170). See
// docs/notes/console.md.
#include "console.h"
#include "ui.h"

#include "ei/hook.h"
#include "text_split.h"

namespace console {

// 005E3E90: the rectangle in pixels, clamped to at least 76x69 and at most the screen; the number
// of 15-pixel lines that fit below the 24-pixel margin, less one; the clamped rectangle back in
// 800x600 coordinates. A shown window gets a new surface.
void ConsoleWindow::SetRect(const RECT* rect) {
    int screenWidth = screen_->width_;
    int screenHeight = screen_->height_;
    screenRect_ = *rect;
    // The 3D interface scales 800x600 to pixels (005F7CA0, Rebirth-patched, so through its address).
    screen_->ScaleRectToScreenEntry(&screenRect_);
    width_ = screenRect_.right - screenRect_.left;
    height_ = screenRect_.bottom - screenRect_.top;
    if (width_ < 0x4C) width_ = 0x4C;
    if (width_ > screenWidth) width_ = screenWidth;
    if (height_ < 0x45) height_ = 0x45;
    if (height_ > screenHeight) height_ = screenHeight;
    visibleLines_ = (height_ - 0x18) / 15 - 1;
    screenRect_.right = screenRect_.left + width_;
    screenRect_.bottom = screenRect_.top + height_;
    rect_ = screenRect_;
    screen_->ScaleRectFromScreenEntry(&rect_);
    if (shown_) {
        Show(false);
        Show(true);
    }
}
EI_REWRITTEN(0x005E3E90, ConsoleWindow::SetRect);

// 005E3F90: keeps the last `lines` lines.
void ConsoleWindow::SetHistory(int lines) {
    history_ = lines;
    if (lines_.GetSize() > lines) lines_.RemoveAt(0, lines_.GetSize() - lines);
    DrawLines();   // 005E4800: clamps the scroll position, draws the visible lines
}
EI_REWRITTEN(0x005E3F90, ConsoleWindow::SetHistory);

// 005E4090: the first piece continues the last line, each '\n' starts a new one. A window scrolled
// back stays on the same lines. Keeps the last history_ lines.
void ConsoleWindow::AddText(const char* text) {
    CStringArray pieces;
    text::Split(text, &pieces, '\n');
    if (lines_.GetSize() != 0)
        lines_[lines_.GetSize() - 1] += *pieces.data;   // the original reads piece 0 unchecked
    else
        lines_.SetAtGrow(lines_.GetSize(), *pieces.data);
    for (int i = 1; i < pieces.GetSize(); ++i) lines_.SetAtGrow(lines_.GetSize(), pieces[i]);
    if (scroll_ != 0) scroll_ += pieces.GetSize();
    if (lines_.GetSize() > history_) lines_.RemoveAt(0, lines_.GetSize() - history_);
    DrawLines();   // 005E4800: clamps the scroll position, draws the visible lines
}
EI_REWRITTEN(0x005E4090, ConsoleWindow::AddText);

} // namespace console
