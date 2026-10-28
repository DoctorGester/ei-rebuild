// The console window's constructor. See console.h and docs/notes/console.md.
#include "console.h"
#include "ui_gdi.h"

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

ConsoleWindow::ConsoleWindow(CInterface3D* screen) {
    screen_ = screen;
    surface_ = nullptr;
    commandIndex_ = kNoCommand;
    caret_ = 0;
    anchor_ = 0;
    scroll_ = 0;
    shown_ = false;
    ctrl_ = false;
    shift_ = false;
    font_ = gdi::PointFont(kFontSize, kFontFace);
    RECT rect = kInitialRect;
    SetRect(&rect);
    history_ = kHistory;
    DropOldLines(kHistory);
    DrawLines();
}

} // namespace console
