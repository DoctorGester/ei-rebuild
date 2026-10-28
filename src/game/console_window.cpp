// The console window's text and placement. See docs/notes/console.md.
#include "console.h"
#include "ui.h"

#include "ei/hook.h"
#include "text_split.h"

namespace console {

// The rectangle in pixels, clamped to at least 76x69 and at most the screen; the number
// of 15-pixel lines that fit below the 24-pixel margin, less one; the clamped rectangle back in
// 800x600 coordinates. A shown window gets a new surface.
void ConsoleWindow::SetRect(const RECT* rect) {
    int screenWidth = screen_->width_;
    int screenHeight = screen_->height_;
    screenRect_ = *rect;
    // The 3D interface scales 800x600 to pixels (Rebirth patches it, so it is called through its address).
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

void ConsoleWindow::SetHistory(int lines) {
    history_ = lines;
    DropOldLines(lines);
    DrawLines();   // clamps the scroll position, draws the visible lines
}
EI_REWRITTEN(0x005E3F90, ConsoleWindow::SetHistory);

void ConsoleWindow::DropOldLines(int count) {
    if (static_cast<int>(lines_.size()) > count) lines_.erase(lines_.begin(), lines_.end() - count);
}

// The first piece continues the last line, each '\n' starts a new one. A window scrolled
// back stays on the same lines. Keeps the last history_ lines.
void ConsoleWindow::AddText(const char* text) {
    std::vector<std::string> pieces;
    text::Split(text, &pieces, '\n');   // at least one piece
    if (!lines_.empty())
        lines_.back() += pieces[0];
    else
        lines_.push_back(pieces[0]);
    lines_.insert(lines_.end(), pieces.begin() + 1, pieces.end());
    if (scroll_ != 0) scroll_ += static_cast<int>(pieces.size());
    DropOldLines(history_);
    DrawLines();   // clamps the scroll position, draws the visible lines
}
EI_REWRITTEN(0x005E4090, ConsoleWindow::AddText);

} // namespace console
