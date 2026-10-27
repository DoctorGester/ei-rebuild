// The console window's constructor. See console.h and docs/notes/console.md.
#include "console.h"
#include "ui_gdi.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "game_data.h"

#include <new>

namespace console {

namespace {
constexpr int kFontSize = 90;            // tenths of a point
constexpr const char* kFontFace = "Courier New";
constexpr RECT kInitialRect = {0, 0, 800, 150};
constexpr int kHistory = 0x100;
constexpr int kNoCommand = -1;
} // namespace

ConsoleWindow* ConsoleWindow::Construct(CInterface3D* screen) {
    new (this) ConsoleWindow;   // commands_, input_ (empty), lines_
    screen_ = screen;
    surface_ = nullptr;
    commandIndex_ = kNoCommand;
    caret_ = 0;
    anchor_ = 0;
    scroll_ = 0;
    shown_ = false;
    ctrl_ = false;
    shift_ = false;
    auto* font = new mfc::impl::CFont();   // null without memory
    font_ = font;
    // A failed allocation still calls CreatePointFont on null (the original does too).
    if (!font->CreatePointFont(kFontSize, kFontFace, nullptr)) {
        delete font_;
        font_ = nullptr;
    }
    RECT rect = kInitialRect;
    SetRect(&rect);
    history_ = kHistory;
    if (lines_.GetSize() > kHistory) lines_.RemoveAt(0, lines_.GetSize() - kHistory);
    DrawLines();
    return this;
}
EI_REWRITTEN(0x005E3CE0, ConsoleWindow::Construct);

} // namespace console
