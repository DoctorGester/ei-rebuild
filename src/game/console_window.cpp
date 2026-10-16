// The console window's text and placement (game.exe 0x005E3E90-0x005E4170). See
// docs/notes/console.md.
#include "console.h"
#include "ui.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_ui.h"

#pragma warning(disable : 4740) // naked forwarders: no optimisation to suppress

namespace console {

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
    // The 3D interface scales 800x600 to pixels (005F7CA0, Rebirth-patched, so through its address).
    reinterpret_cast<const CInterface3D*>(screen_)->CallScaleRectToScreen(reinterpret_cast<RECT*>(screenRect_));
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
    InterfaceScaleRectFromScreenOriginal(reinterpret_cast<CInterface3D*>(screen_), nullptr,
                                         reinterpret_cast<tagRECT*>(rect_));
    if (shown_) {
        Show(false);
        Show(true);
    }
}
EI_REPLACE_METHOD(0x005E3E90, ConsoleWindow::SetRect);

// 005E3F90: keeps the last `lines` lines.
void ConsoleWindow::SetHistory(int lines) {
    history_ = lines;
    if (lines_.GetSize() > lines) lines_.RemoveAt(0, lines_.GetSize() - lines);
    static_cast<ConsoleWindow*>(this)->DrawLines();   // 005E4800: clamps the scroll position, draws the visible lines
}
EI_REPLACE_METHOD(0x005E3F90, ConsoleWindow::SetHistory);

// 005E4090: the first piece continues the last line, each '\n' starts a new one. A window scrolled
// back stays on the same lines. Keeps the last history_ lines.
void ConsoleWindow::AddText(const char* text) {
    CStringArray pieces;
    StringSplit(text, &pieces, '\n');
    if (lines_.GetSize() != 0)
        lines_[lines_.GetSize() - 1] += *pieces.data;   // the original reads piece 0 unchecked
    else
        lines_.SetAtGrow(lines_.GetSize(), *pieces.data);
    for (int i = 1; i < pieces.GetSize(); ++i) lines_.SetAtGrow(lines_.GetSize(), pieces[i]);
    if (scroll_ != 0) scroll_ += pieces.GetSize();
    if (lines_.GetSize() > history_) lines_.RemoveAt(0, lines_.GetSize() - history_);
    static_cast<ConsoleWindow*>(this)->DrawLines();   // 005E4800: clamps the scroll position, draws the visible lines
}
EI_REPLACE_METHOD(0x005E4090, ConsoleWindow::AddText);

} // namespace console
