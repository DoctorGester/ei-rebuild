// The interface's key-down handler (CInterface3D, 005F78C0). See ui.h and docs/notes/ui.md.
//
// The modifier flags it sets are +0x7C Ctrl (scan codes 0x1D, 0x11D), +0x7D Shift (0x2A, 0x36) and
// +0x7E Alt (0x38, 0x138); ui.h's field names for them are provisional and do not match.
#include "ui.h"

#include "console.h"
#include "dib.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace {

void SetByte(void* p, int offset, std::uint8_t value) { static_cast<std::uint8_t*>(p)[offset] = value; }

// The scan code to character table (0079BF70, 0x200 entries).
std::uint8_t KeyCharacter(int scan) { return reinterpret_cast<const std::uint8_t*>(0x0079BF70)[scan]; }

// The screen capture (CallCaptureSurface on [007AF080] + 0x18); the picture (dib.h) is written with
// CDib::Write and deleted through its slot 1.
// CFile::CFile(name, flags) (0070108B, RET 8).
__declspec(naked) void __fastcall ConstructFile(void*, void*, const char*, unsigned) EI_ORIGINAL(0x0070108B)

std::uint32_t& ShotCounter() { return *reinterpret_cast<std::uint32_t*>(0x007C7E24); }

constexpr int kRepeat = 0x4000;

// The input side's key-down handler (slot 9) of a window.
void WindowKeyDown(std::uint8_t* window, int key, int repeat, int flags) {
    void* input = window + 0x94;
    using KeyFn = void(__thiscall*)(void*, int, int, int);
    reinterpret_cast<KeyFn>((*static_cast<void***>(input))[9])(input, key, repeat, flags);
}

} // namespace

void CInterface3D::OnKeyDown(int key, int repeat, int flags) {
    auto* self = reinterpret_cast<std::uint8_t*>(this);
    console::ConsoleWindow* consoleWindow;
    std::memcpy(&consoleWindow, self + 0x14, 4);
    int scan = flags & 0x1FF;
    switch (scan) {
    case 0x1D:
    case 0x11D:   // Ctrl
        if (flags & kRepeat) return;
        SetByte(self, 0x7C, 1);
        break;
    case 0x2A:
    case 0x36:    // Shift
        if (flags & kRepeat) return;
        SetByte(self, 0x7D, 1);
        break;
    case 0x38:
    case 0x138:   // Alt
        if (flags & kRepeat) return;
        SetByte(self, 0x7E, 1);
        break;
    case 0x29:    // the key under Esc
        consoleWindow->Show(reinterpret_cast<const std::uint8_t*>(consoleWindow)[0x7A] == 0);
        return;
    case 0x46: {  // Scroll Lock: a screenshot
        void* holder = *reinterpret_cast<void* const*>(0x007AF080);
        CDXSurface* capturer;
        std::memcpy(&capturer, static_cast<std::uint8_t*>(holder) + 0x18, 4);
        CDib* picture = CallCaptureSurface(capturer);
        if (!picture) return;
        mfc::CString name;
        name.Format(reinterpret_cast<const char*>(0x007930B4), ShotCounter());   // "shot%.4d.bmp"
        ++ShotCounter();
        alignas(4) std::uint8_t storage[sizeof(mfc::CFile)];
        auto* file = reinterpret_cast<mfc::CFile*>(storage);
        ConstructFile(file, nullptr, name, 0x1001);   // modeCreate | modeWrite
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

    if (reinterpret_cast<const std::uint8_t*>(consoleWindow)[0x7A] != 0) {
        consoleWindow->OnKeyDown(key, repeat, flags);
        return;
    }
    std::uint8_t* chat;
    std::memcpy(&chat, self + 0x1C, 4);
    if ((IsMultiplayerClient() & 0xFF) && chat[0x14C] != 0) {   // TEST AL,AL
        WindowKeyDown(chat, key, repeat, flags);
        return;
    }
    if (stackBegin_ != stackEnd_) WindowKeyDown(reinterpret_cast<std::uint8_t*>(stackEnd_[-1]), key, repeat, flags);
    if (stackBegin_ != stackEnd_ && reinterpret_cast<const std::uint8_t*>(stackEnd_[-1])[0x9C] == 0 &&
        KeyCharacter(scan) == ';') {
        ShowHelp(1);
    }
}

namespace {
void __fastcall InterfaceKeyDown(CInterface3D* self, void*, int key, int repeat, int flags) {
    self->OnKeyDown(key, repeat, flags);
}
} // namespace
EI_REPLACE(0x005F78C0, InterfaceKeyDown);
