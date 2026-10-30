// The interface's key-down handler (Interface3D). See ui.h and docs/notes/ui.md.
// The modifier flags it sets are Ctrl (scan codes 0x1D, 0x11D), Shift (0x2A, 0x36) and Alt (0x38, 0x138).
#include "ui.h"

#include "console.h"
#include "dib.h"
#include "texture.h"
#include "world.h"

#include <cstdint>
#include <string>
#include "platform/file.h"
#include "game_singletons.h"

#include <cstring>
#include <format>

namespace {

// The key bindings' action of a scan code (g_keyActions), as a byte.
std::uint8_t KeyCharacter(int scan) { return static_cast<std::uint8_t>(g_keyActions[scan]); }

std::uint32_t g_shotCounter = /*007C7E24*/ {};

constexpr int kRepeat = 0x4000;

// The input side's key-down handler of a window.
void WindowKeyDown(UiWindow* window, int key, int repeat, int flags) {
    window->Input()->Event(UiInputSide::kEventKeyDown, key, repeat, flags);
}

} // namespace

void Interface3D::OnKeyDown(int key, int repeat, int flags) {
    console::ConsoleWindow* consoleWindow = console;
    int scan = flags & 0x1FF;
    switch (scan) {
    case 0x1D:
    case 0x11D:   // Ctrl
        if (flags & kRepeat) return;
        ctrlDown = true;
        break;
    case 0x2A:
    case 0x36:    // Shift
        if (flags & kRepeat) return;
        shiftDown = 1;
        break;
    case 0x38:
    case 0x138:   // Alt
        if (flags & kRepeat) return;
        altDown = true;
        break;
    case 0x29:    // the key under Esc
        consoleWindow->Show(!consoleWindow->shown);
        return;
    case 0x46: {  // Scroll Lock: a screenshot
        Dib* picture = CaptureScreen(g_sceneRenderer.device);
        if (!picture) return;
        std::string name;
        name = std::format("shot{:04}.bmp", g_shotCounter);
        ++g_shotCounter;
        platform::File file(name, platform::File::modeCreate | platform::File::modeWrite);
        picture->Write(&file);
        file.Close();
        delete picture;
        return;
    }
    default:
        break;
    }

    if (consoleWindow->shown) {
        consoleWindow->OnKeyDown(key, repeat, flags);
        return;
    }
    ChatOverlay* chat = root;
    if (IsMultiplayerClient() && chat->chatOpen) {
        WindowKeyDown(chat, key, repeat, flags);
        return;
    }
    if (!stack.empty()) WindowKeyDown(stack.back(), key, repeat, flags);
    if (!stack.empty() && !stack.back()->Input()->handled &&
        KeyCharacter(scan) == ';') {
        OpenHelp(true);
    }
}
