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

// 005E44A0 (same prologue): side by side on the fields, except Enter, which executes the line:
// there ours only, and the line is empty afterwards with the caret at 0. (Ctrl+V reads the
// clipboard twice, Ctrl+C / X write it twice: the same text.)
void(__thiscall* s_onChar)(ConsoleWindow*, int, int, int);
void __fastcall OnCharVerify(ConsoleWindow* self, void*, int key, int repeat, int flags) {
    if (key == 13) {
        self->OnChar(key, repeat, flags);
        bool ok = Text(self->input_).empty() && self->caret_ == 0 && self->anchor_ == 0;
        ei::verify::Check("ConsoleWindow::OnChar", true, ok, "enter: [%s] caret %d", Text(self->input_).c_str(), self->caret_);
        return;
    }
    WindowFields before(self);
    s_onChar(self, key, repeat, flags);
    WindowFields expected(self);
    before.Restore(self);
    self->OnChar(key, repeat, flags);
    WindowFields actual(self);
    ei::verify::Check("ConsoleWindow::OnChar", true, expected == actual, "key %x: %s vs %s", key,
                      expected.Describe().c_str(), actual.Describe().c_str());
}
EI_VERIFY(0x005E44A0, OnCharVerify, 8, s_onChar);

// 005E3FD0: ours only (creates or releases the surface): the flag, and a surface exactly while shown.
void __fastcall ShowVerify(ConsoleWindow* self, void*, bool shown) {
    self->Show(shown);
    ei::verify::Check("ConsoleWindow::Show", true, self->shown_ == shown && (self->surface_ != nullptr) == shown,
                      "shown %d surface %p", shown, self->surface_);
}
EI_VERIFY(0x005E3FD0, ShowVerify, 0, s_unused);

// ---- Text layout -------------------------------------------------------------------------------

// The caller holds the surface's DC (+0x08) while it draws text, so the pixels are read and put
// back through GDI (a DirectDraw lock would collide with the DC).
struct GdiPixels {
    std::vector<std::uint32_t> bits;
};

bool CopySurfaceDC(HDC dc, int width, int height, GdiPixels* read, const GdiPixels* write) {
    if (!dc || width <= 0 || height <= 0) return false;
    HDC mem = CreateCompatibleDC(dc);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = mem && bitmap;
    if (ok) {
        HGDIOBJ old = SelectObject(mem, bitmap);
        auto* pixels = static_cast<std::uint32_t*>(bits);
        std::size_t count = static_cast<std::size_t>(width) * height;
        if (write) {
            std::memcpy(pixels, write->bits.data(), count * 4);
            ok = BitBlt(dc, 0, 0, width, height, mem, 0, 0, SRCCOPY) != 0;
        } else {
            ok = BitBlt(mem, 0, 0, width, height, dc, 0, 0, SRCCOPY) != 0;
            GdiFlush();
            read->bits.assign(pixels, pixels + count);
        }
        SelectObject(mem, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (mem) DeleteDC(mem);
    return ok;
}

// 005FDFA0: PUSH -1; PUSH imm32 (7). Side by side: the surface's pixels are recorded, the
// original draws, its pixels are recorded and the old ones put back, ours draws; the line counts
// and the pixels are compared.
int(__thiscall* s_drawWrapped)(CUiSurface*, const char*, const RECT*, int, int, COLORREF, int, int);
int __fastcall DrawWrappedVerify(CUiSurface* self, void*, const char* text, const RECT* rect, int maxLines, int font,
                                 COLORREF colour, int format, int flag) {
    HDC dc = *reinterpret_cast<HDC*>(reinterpret_cast<std::uint8_t*>(self) + 0x08);
    int width = self->rect_.right - self->rect_.left;
    int height = self->rect_.bottom - self->rect_.top;
    std::string copy = text ? text : "";
    RECT rectCopy = *rect;
    GdiPixels before, expected, actual;
    bool pixels = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(self) + 0x10) &&
                  CopySurfaceDC(dc, width, height, &before, nullptr);
    int result = s_drawWrapped(self, copy.c_str(), &rectCopy, maxLines, font, colour, format, flag);
    if (pixels) pixels = CopySurfaceDC(dc, width, height, &expected, nullptr) && CopySurfaceDC(dc, width, height, nullptr, &before);
    rectCopy = *rect;
    int mine = self->DrawWrappedText(copy.c_str(), &rectCopy, maxLines, font, colour, format, flag);
    bool same = true;
    if (pixels && CopySurfaceDC(dc, width, height, &actual, nullptr)) same = expected.bits == actual.bits;
    ei::verify::Check("CUiSurface::DrawWrappedText", true, result == mine && same, "'%.80s' lines %d/%d pixels %d",
                      copy.c_str(), result, mine, same);
    return result;
}
EI_VERIFY(0x005FDFA0, DrawWrappedVerify, 7, s_drawWrapped);

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

// 005F8070 (CInterface3D::OnStringEvent) opens windows and queues texts: ours only. "autosave"
// raises +0x81; any event leaves the window stack consistent (begin <= end).
void __fastcall OnStringEventVerify(CInterface3D* self, void*, const char* event) {
    self->OnStringEvent(event);
    bool ok = self->stackBegin_ <= self->stackEnd_;
    if (ok && event && _strnicmp(event, "autosave", 8) == 0 && (event[8] == 0 || event[8] == ' '))
        ok = reinterpret_cast<const std::uint8_t*>(self)[0x81] != 0;
    ei::verify::Check("CInterface3D::OnStringEvent", true, ok, "%.60s", event ? event : "(null)");
}
EI_VERIFY(0x005F8070, OnStringEventVerify, 0, s_unused);

} // namespace
