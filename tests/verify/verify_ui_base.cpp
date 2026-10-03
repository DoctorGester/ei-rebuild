// Checks for the interface base (ui_console_window.cpp, ui_interface_frame.cpp).
//
// The console window's two draw functions run side by side and compare the surface's pixels:
// the original draws, its pixels and the clamped fields are recorded, the fields and the pixels
// are put back with the rows the function paints inverted, then ours draws and both are
// compared. (Each function paints whole rows of its band, so anything ours left undrawn shows.)
#include "verify.h"

#include "../../src/game/console.h"
#include "../../src/game/ui.h"
#include "../../src/lib/gameapi.h"

#include <windows.h>
#include <ddraw.h>

#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

void* s_unused;

using namespace console;

std::string Text(const mfc::CString& s) { return std::string(static_cast<const char*>(s), Length(s)); }

// ---- Surface pixels ----------------------------------------------------------------------------

// The DirectDraw surface of an interface surface (CUiSurface +0x10 -> +0x10), locked the way the
// game locks it (under its critical section 007AEE78).
IDirectDrawSurface7* DirectDrawSurface(void* surface) {
    std::uint8_t* inner = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(surface) + 0x10);
    return inner ? *reinterpret_cast<IDirectDrawSurface7**>(inner + 0x10) : nullptr;
}

struct Pixels {
    int width = 0;
    int height = 0;
    int rowBytes = 0;
    std::vector<std::uint8_t> bytes;
};

CRITICAL_SECTION* SurfaceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// Reads the surface (or, with `write`, stores `pixels` into it, the rows [invertFrom, invertTo)
// inverted). Returns false when the surface cannot be locked.
bool AccessPixels(void* surface, Pixels& pixels, bool write, int invertFrom = 0, int invertTo = 0) {
    IDirectDrawSurface7* dd = DirectDrawSurface(surface);
    if (!dd) return false;
    gameapi::EnterCriticalSection(SurfaceLock());
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    bool ok = dd->Lock(nullptr, &desc, DDLOCK_WAIT, nullptr) == DD_OK;
    if (ok) {
        auto* base = static_cast<std::uint8_t*>(desc.lpSurface);
        int rowBytes = static_cast<int>(desc.dwWidth * (desc.ddpfPixelFormat.dwRGBBitCount / 8));
        if (!write) {
            pixels.width = static_cast<int>(desc.dwWidth);
            pixels.height = static_cast<int>(desc.dwHeight);
            pixels.rowBytes = rowBytes;
            pixels.bytes.resize(static_cast<std::size_t>(rowBytes) * desc.dwHeight);
            for (DWORD y = 0; y < desc.dwHeight; ++y)
                std::memcpy(&pixels.bytes[y * rowBytes], base + y * desc.lPitch, rowBytes);
        } else if (pixels.rowBytes == rowBytes && pixels.height == static_cast<int>(desc.dwHeight)) {
            for (int y = 0; y < pixels.height; ++y) {
                std::uint8_t* row = base + y * desc.lPitch;
                std::memcpy(row, &pixels.bytes[y * rowBytes], rowBytes);
                if (y >= invertFrom && y < invertTo)
                    for (int i = 0; i < rowBytes; ++i) row[i] = static_cast<std::uint8_t>(~row[i]);
            }
        }
        dd->Unlock(nullptr);
    }
    gameapi::LeaveCriticalSection(SurfaceLock());
    return ok;
}

// First differing pixel row, or -1.
int FirstDifferentRow(const Pixels& a, const Pixels& b) {
    if (a.bytes.size() != b.bytes.size() || a.rowBytes != b.rowBytes) return -2;
    for (int y = 0; y < a.height; ++y)
        if (std::memcmp(&a.bytes[y * a.rowBytes], &b.bytes[y * b.rowBytes], a.rowBytes) != 0) return y;
    return -1;
}

// ---- Console window ----------------------------------------------------------------------------

struct WindowFields {
    std::string input;
    int caret, anchor, commandIndex, scroll;
    bool ctrl, shift;

    explicit WindowFields(const ConsoleWindow* w)
        : input(Text(w->input_)), caret(w->caret_), anchor(w->anchor_), commandIndex(w->commandIndex_),
          scroll(w->scroll_), ctrl(w->ctrl_), shift(w->shift_) {}
    void Restore(ConsoleWindow* w) const {
        w->input_ = mfc::CString(input.c_str());
        w->caret_ = caret;
        w->anchor_ = anchor;
        w->commandIndex_ = commandIndex;
        w->scroll_ = scroll;
        w->ctrl_ = ctrl;
        w->shift_ = shift;
    }
    std::string Describe() const {
        char buf[96];
        std::snprintf(buf, sizeof(buf), " caret %d anchor %d command %d scroll %d ctrl %d shift %d", caret, anchor,
                      commandIndex, scroll, ctrl, shift);
        return "[" + input + "]" + buf;
    }
    bool operator==(const WindowFields& o) const {
        return input == o.input && caret == o.caret && anchor == o.anchor && commandIndex == o.commandIndex &&
               scroll == o.scroll && ctrl == o.ctrl && shift == o.shift;
    }
};

// Draw functions: fields always, pixels when the window has a surface and a font.
void CompareDraw(const char* name, ConsoleWindow* w, int bandFrom, int bandTo, const std::function<void()>& original,
                 const std::function<void()>& ours) {
    WindowFields before(w);
    Pixels pre;
    bool drawn = w->surface_ && w->font_ && AccessPixels(w->surface_, pre, false);
    original();
    WindowFields expected(w);
    Pixels expectedPixels;
    if (drawn) AccessPixels(w->surface_, expectedPixels, false);
    before.Restore(w);
    if (drawn) AccessPixels(w->surface_, pre, true, bandFrom, bandTo);
    ours();
    WindowFields actual(w);
    int row = -1;
    if (drawn) {
        Pixels actualPixels;
        AccessPixels(w->surface_, actualPixels, false);
        row = FirstDifferentRow(expectedPixels, actualPixels);
    }
    ei::verify::Check(name, true, expected == actual && row == -1, "%s vs %s, pixel row %d", expected.Describe().c_str(),
                      actual.Describe().c_str(), row);
}

// 005E4800 / 005E4A10: MOV EAX,FS:[0]; PUSH -1 (8).
void(__thiscall* s_drawLines)(ConsoleWindow*);
void __fastcall DrawLinesVerify(ConsoleWindow* self, void*) {
    CompareDraw("ConsoleWindow::DrawLines", self, 0, self->height_ - 0x1F, [&] { s_drawLines(self); },
                [&] { self->DrawLines(); });
}
EI_VERIFY(0x005E4800, DrawLinesVerify, 8, s_drawLines);

void(__thiscall* s_drawInput)(ConsoleWindow*);
void __fastcall DrawInputVerify(ConsoleWindow* self, void*) {
    CompareDraw("ConsoleWindow::DrawInput", self, self->height_ - 0x1F, self->height_, [&] { s_drawInput(self); },
                [&] { self->DrawInput(); });
}
EI_VERIFY(0x005E4A10, DrawInputVerify, 8, s_drawInput);

// 005E4180 (MOV EAX,FS:[0]; PUSH -1, 8): side by side on the fields it edits (both redraw).
void(__thiscall* s_onKeyDown)(ConsoleWindow*, int, int, int);
void __fastcall OnKeyDownVerify(ConsoleWindow* self, void*, int key, int repeat, int flags) {
    WindowFields before(self);
    s_onKeyDown(self, key, repeat, flags);
    WindowFields expected(self);
    before.Restore(self);
    self->OnKeyDown(key, repeat, flags);
    WindowFields actual(self);
    ei::verify::Check("ConsoleWindow::OnKeyDown", true, expected == actual, "key %x: %s vs %s", key,
                      expected.Describe().c_str(), actual.Describe().c_str());
}
EI_VERIFY(0x005E4180, OnKeyDownVerify, 8, s_onKeyDown);

// 005E3FD0: ours only (creates or releases the surface): the flag, and a surface exactly while shown.
void __fastcall ShowVerify(ConsoleWindow* self, void*, bool shown) {
    self->Show(shown);
    ei::verify::Check("ConsoleWindow::Show", true, self->shown_ == shown && (self->surface_ != nullptr) == shown,
                      "shown %d surface %p", shown, self->surface_);
}
EI_VERIFY(0x005E3FD0, ShowVerify, 0, s_unused);

// ---- Interface frame ---------------------------------------------------------------------------

// 005F57C0: ours only (it closes, deletes and activates windows). Checked afterwards: the mouse
// position is the one the window procedure stored (007C7C50). The pending flag and the queued
// window are not checked: the windows' own updates, which run last, may raise them again.
void __fastcall InterfaceUpdateVerify(CInterface3D* self, void*) {
    self->Update();
    bool ok = self->mouseX_ == *reinterpret_cast<int*>(0x007C7C50) && self->mouseY_ == *reinterpret_cast<int*>(0x007C7C54);
    ei::verify::Check("CInterface3D::Update", true, ok, "mouse %d,%d", self->mouseX_, self->mouseY_);
}
EI_VERIFY(0x005F57C0, InterfaceUpdateVerify, 0, s_unused);

} // namespace
