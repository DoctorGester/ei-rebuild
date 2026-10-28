// The interface's key-down handler (CInterface3D). See ui.h and docs/notes/ui.md.
// The modifier flags it sets are Ctrl (scan codes 0x1D, 0x11D), Shift (0x2A, 0x36) and Alt (0x38, 0x138).
#include "ui.h"

#include "console.h"
#include "dib.h"
#include "texture.h"
#include "world.h"

#include "lib/static_object.h"
#include <cstdint>
#include <string>
#include "lib/file_io.h"
#include "game_singletons.h"
#include "lib/str.h"

#include <cstring>

namespace {

// The key bindings' action of a scan code (ui.h KeyActions), as a byte.
std::uint8_t KeyCharacter(int scan) { return static_cast<std::uint8_t>(KeyActions()[scan]); }

std::uint32_t g_shotCounter = /*007C7E24*/ {};

constexpr int kRepeat = 0x4000;

// The input side's key-down handler of a window.
void WindowKeyDown(CUiWindow* window, int key, int repeat, int flags) {
    window->Input()->Event(UiInputSide::kEventKeyDown, key, repeat, flags);
}

} // namespace

void CInterface3D::OnKeyDown(int key, int repeat, int flags) {
    console::ConsoleWindow* consoleWindow = console_;
    int scan = flags & 0x1FF;
    switch (scan) {
    case 0x1D:
    case 0x11D:   // Ctrl
        if (flags & kRepeat) return;
        ctrlDown_ = true;
        break;
    case 0x2A:
    case 0x36:    // Shift
        if (flags & kRepeat) return;
        shiftDown_ = 1;
        break;
    case 0x38:
    case 0x138:   // Alt
        if (flags & kRepeat) return;
        altDown_ = true;
        break;
    case 0x29:    // the key under Esc
        consoleWindow->Show(!consoleWindow->shown_);
        return;
    case 0x46: {  // Scroll Lock: a screenshot
        CDXSurface* capturer = TheRenderDevice()->renderTarget_;
        CDib* picture = CaptureSurface(capturer);
        if (!picture) return;
        std::string name;
        name = str::Format("shot%.4d.bmp", g_shotCounter);
        ++g_shotCounter;
        lib::File file(name, lib::File::modeCreate | lib::File::modeWrite);
        picture->Write(&file);
        file.Close();
        delete picture;
        return;
    }
    default:
        break;
    }

    if (consoleWindow->shown_) {
        consoleWindow->OnKeyDown(key, repeat, flags);
        return;
    }
    CChatOverlay* chat = root_;
    if ((IsMultiplayerClient() & 0xFF) && chat->chatOpen_) {   // the low byte
        WindowKeyDown(chat, key, repeat, flags);
        return;
    }
    if (!stack_.empty()) WindowKeyDown(stack_.back(), key, repeat, flags);
    if (!stack_.empty() && !stack_.back()->Input()->handled_ &&
        KeyCharacter(scan) == ';') {
        OpenHelp(1);
    }
}
