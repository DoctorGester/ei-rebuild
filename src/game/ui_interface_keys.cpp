// The interface's key-down handler (CInterface3D, 005F78C0). See ui.h and docs/notes/ui.md.
//
// The modifier flags it sets are +0x7C Ctrl (scan codes 0x1D, 0x11D), +0x7D Shift (0x2A, 0x36) and
// +0x7E Alt (0x38, 0x138).
#include "ui.h"

#include "console.h"
#include "dib.h"
#include "texture.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/mfc_file.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

// The scan code to character table (0079BF70, 0x200 entries).
std::uint8_t KeyCharacter(int scan) { return ei::GamePointer<const std::uint8_t>(0x0079BF70)[scan]; }

// The screen capture (CallCaptureSurface on [007AF080] + 0x18); the picture (dib.h) is written with
// CDib::Write and deleted through its slot 1.

std::uint32_t& ShotCounter() { return ei::GameData<std::uint32_t>(0x007C7E24); }

constexpr int kRepeat = 0x4000;

// The input side's key-down handler (slot 9) of a window.
void WindowKeyDown(CUiWindow* window, int key, int repeat, int flags) {
    window->Input()->Event(UiInputSide::kKeyDown, key, repeat, flags);
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
        CDXSurface* capturer = RenderDeviceObject()->renderTarget_;
        CDib* picture = CaptureSurface(capturer);
        if (!picture) return;
        mfc::CString name;
        name.Format("shot%.4d.bmp", ShotCounter());
        ++ShotCounter();
        alignas(4) std::uint8_t storage[sizeof(mfc::CFile)];
        auto* file = reinterpret_cast<mfc::CFile*>(storage);
        reinterpret_cast<mfc::impl::CFile*>(file)->ConstructOpen(name, 0x1001);   // 0070108B: modeCreate | modeWrite
        picture->Write(file);
        file->Close();
        using DeleteFn = void(__thiscall*)(void*, int);
        reinterpret_cast<DeleteFn>((*reinterpret_cast<void***>(picture))[1])(picture, 1);
        file->~CFile();
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
    if ((IsMultiplayerClient() & 0xFF) && chat->chatOpen_) {   // TEST AL,AL
        WindowKeyDown(chat, key, repeat, flags);
        return;
    }
    if (stackBegin_ != stackEnd_) WindowKeyDown(stackEnd_[-1], key, repeat, flags);
    if (stackBegin_ != stackEnd_ && !stackEnd_[-1]->Input()->handled_ &&
        KeyCharacter(scan) == ';') {
        OpenHelp(1);
    }
}

EI_REPLACE_METHOD(0x005F78C0, CInterface3D::OnKeyDown);
