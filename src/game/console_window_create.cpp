// The console window's constructor. See console.h and docs/notes/console.md.
#include "console.h"

#include <new>

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
