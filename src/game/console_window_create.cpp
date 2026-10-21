// The console window's constructor (005E3CE0). See console.h and docs/notes/console.md.
#include "console.h"
#include "ui_gdi.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "game_data.h"

#include <new>

namespace console {

namespace {
constexpr int kFontSize = 90;            // tenths of a point
constexpr const char* kFontFace = "Courier New";
constexpr int kInitialRect[4] = {0, 0, 800, 150};
constexpr int kHistory = 0x100;
constexpr int kNoCommand = -1;
// MFC CFont's vtable (an empty CGdiObject: no handle yet).
void** FontVtable() { return ei::GamePointer<void*>(0x0073BFB4); }
} // namespace

ConsoleWindow* ConsoleWindow::Construct(int* screen) {
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
    auto* font = static_cast<ui_gdi::GameFont*>(gamecrt::operator_new(sizeof(ui_gdi::GameFont)));
    if (font) {
        font->handle = nullptr;
        font->vtable = FontVtable();
    }
    font_ = font;
    // As the original: a failed allocation still calls CreatePointFont on null.
    if (!font->CreatePointFont(kFontSize, kFontFace, nullptr)) {
        if (font_) font_->Delete();
        font_ = nullptr;
    }
    int rect[4] = {kInitialRect[0], kInitialRect[1], kInitialRect[2], kInitialRect[3]};
    static_cast<ConsoleWindow*>(this)->SetRect(rect);
    history_ = kHistory;
    if (lines_.GetSize() > kHistory) lines_.RemoveAt(0, lines_.GetSize() - kHistory);
    static_cast<ConsoleWindow*>(this)->DrawLines();
    return this;
}

} // namespace console
