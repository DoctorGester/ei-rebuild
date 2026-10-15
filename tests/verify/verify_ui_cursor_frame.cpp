// Checks for the software cursor's frame draw and its draws to the primary (ui_cursor_frame.cpp),
// and the cursor images' constructor (ui_cursor.cpp).
#include "verify.h"

#include "../../src/game/dx_surface.h"
#include "../../src/game/texture.h"
#include "../../src/game/ui_cursor.h"
#include "../../src/render/render.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

void __fastcall CursorDrawFrame(CCursor* self);
void __fastcall CursorThread(CCursor* self);

// 005E54A0 blits: ours only. Checked: when it composed the cursor (an image shown and a non-empty
// rectangle), the drawn rectangle is the cursor's rectangle afterwards.
static void* s_cursorFrame;
static void __fastcall CursorDrawFrameVerify(CCursor* self) {
    CursorDrawFrame(self);
    bool software = (*reinterpret_cast<unsigned char*>(0x007AF064) & 1) != 0;   // the renderer's +4 bit 0
    bool composed = software && self->imageShown_ != 0 && self->enabled_ != 0 && !IsRectEmpty(&self->rect_);
    bool ok = !composed || std::memcmp(&self->drawn_, &self->rect_, sizeof(RECT)) == 0;
    ei::verify::Check("CursorDrawFrame", true, ok, "drawn rectangle not the cursor's");
}
EI_VERIFY(0x005E54A0, CursorDrawFrameVerify, 0, s_cursorFrame);

// 005E5640, a Blt between two holders: the original (8 stolen bytes: PUSH ESI; MOV ESI,ECX;
// PUSH 007AEE78), then ours (the same copy again); results compared. With the software cursor
// (the frame draw and the cursor thread).
using HolderBltFn = long(__fastcall*)(CDXSurface*, void*, RECT*, CDXSurface*, RECT*, unsigned long, void*);
static HolderBltFn s_holderBlt;
static long __fastcall HolderBltVerify(CDXSurface* self, void*, RECT* to, CDXSurface* source, RECT* from, unsigned long flags,
                                       void* fx) {
    long expected = s_holderBlt(self, nullptr, to, source, from, flags, fx);
    long result = self->Blt(to, source, from, flags, fx);
    ei::verify::Check("HolderBlt", expected, result, "result %08lX, original %08lX", static_cast<unsigned long>(result),
                      static_cast<unsigned long>(expected));
    return result;
}
EI_VERIFY(0x005E5640, HolderBltVerify, 8, s_holderBlt);

// 005E5690 is the cursor thread's loop: ours only. Checked when it returns: it was asked to stop
// (+0x9C) or the cursor is off (+0x91 0). Started in every scenario (it draws only with the
// software cursor bit).
static void* s_cursorThread;
static void __fastcall CursorThreadVerify(CCursor* self) {
    CursorThread(self);
    bool ok = self->enabled_ == 0 || self->stop_ != 0;
    ei::verify::Check("CursorThread", true, ok, "returned without a stop request");
}
EI_VERIFY(0x005E5690, CursorThreadVerify, 0, s_cursorThread);

// 005E52A0, the thread's entry (S16b): ours only, exit code 0 (the loop's own check runs inside).
unsigned __cdecl CursorThreadEntry(void* cursor);
static unsigned __cdecl CursorThreadEntryVerify(void* cursor) {
    unsigned code = CursorThreadEntry(cursor);
    ei::verify::Check("CursorThreadEntry", 0u, code, "exit code %u", code);
    return code;
}
static void* s_cursorThreadEntry;
EI_VERIFY(0x005E52A0, CursorThreadEntryVerify, 0, s_cursorThreadEntry);

// 005E6630 copies the screen under the cursor into its background surface: ours only. Checked: the
// drawn rectangle and the bounds (+0x50) are unchanged. Every scenario that loads a zone (the
// loading movie calls it).
static void* s_saveBackground;
static void __fastcall CursorSaveBackgroundVerify(CCursor* self, void*, char fromPrimary) {
    RECT drawn = self->drawn_, bounds = self->bounds_;
    self->SaveBackground(fromPrimary);
    bool ok = std::memcmp(&drawn, &self->drawn_, sizeof(RECT)) == 0 && std::memcmp(&bounds, &self->bounds_, sizeof(RECT)) == 0;
    ei::verify::Check("CursorSaveBackground", true, ok, "rectangles changed");
}
EI_VERIFY(0x005E6630, CursorSaveBackgroundVerify, 0, s_saveBackground);

// --- 005E4EB0 (ui_cursor.cpp, ticket R7) ------------------------------------------------------------

namespace {

template <typename... Args>
long SurfaceSlot(void* surface, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(surface))[slot])(surface, args...);
}

// The holder's pixels for reading, described in `desc`: the new renderer's memory, or a read-only
// Lock of its surface (`locked` set; Unlock afterwards).
const std::uint8_t* ReadPixels(CDXSurface* holder, std::uint32_t* desc, void** locked) {
    *locked = nullptr;
    if (!ClientPixels(holder, desc)) {
        if (!holder->surface) return nullptr;
        std::memset(desc, 0, 0x7C);
        desc[0] = 0x7C;
        // DDLOCK_WAIT | DDLOCK_READONLY
        if (SurfaceSlot(holder->surface, 25, static_cast<RECT*>(nullptr), static_cast<void*>(desc), 0x11ul,
                        static_cast<void*>(nullptr)) != 0)
            return nullptr;
        *locked = holder->surface;
    }
    return reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(desc[9]));
}

// The first pixel whose mask is not all ones exactly where its colour bits are 0; -1 when none.
int FirstBadMask(const CursorImage* image, const std::uint8_t* pixels, std::uint32_t width, std::uint32_t height,
                 std::uint32_t pitch, int bytes) {
    std::uint32_t colourBits = bytes == 2 ? 0xFFFFu : 0xFFFFFFu, ones = bytes == 2 ? 0xFFFFu : 0xFFFFFFFFu;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint32_t pixel = 0, mask = 0;
            std::memcpy(&pixel, pixels + y * pitch + x * static_cast<std::uint32_t>(bytes), static_cast<std::size_t>(bytes));
            std::memcpy(&mask, image->saved + (y * width + x) * static_cast<std::uint32_t>(bytes), static_cast<std::size_t>(bytes));
            if (mask != ((pixel & colourBits) == 0 ? ones : 0u)) return static_cast<int>(y * width + x);
        }
    }
    return -1;
}

} // namespace

// 005E4EB0 loads a cursor image (the picture from the texture manager, its holder, the mask): ours
// only (the original would make a second holder; it cannot read a holder without a DirectDraw
// surface). Checked: the result is the image; its holder has pixels (a surface, or in the switch-over
// with "cpuwindows" only the new renderer's memory); frames = the picture's width / height, a frame's
// width = the width / frames, the height the picture's, frame 0; the hot spot scaled to the cursor's
// size (007C7C90 / 94, from 32); the mask all ones exactly where the picture's colour bits are 0.
// Every scenario (the cursor's shapes are loaded at startup: 005DAEA0 -> 005E63F0).
static void* s_cursorImage;
static CursorImage* __fastcall CursorImageVerify(CursorImage* self, void*, const char* name, int hotX, int hotY,
                                                 int flag) {
    CursorImage* result = self->Construct(name, hotX, hotY, flag);
    CDXSurface* holder = self->holder;
    bool ok = result == self && holder && (holder->surface || render::SurfaceMemoryOf(holder)) && self->frame == 0;
    std::uint32_t desc[0x1F] = {};
    int bad = -2;
    if (ok) {
        auto* lock = reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78);
        ::EnterCriticalSection(lock);
        void* locked = nullptr;
        const std::uint8_t* pixels = ReadPixels(holder, desc, &locked);
        std::uint32_t width = desc[3], height = desc[2];
        int bytes = holder->bitsPerPixel / 8;
        LONG cursorWidth = *reinterpret_cast<LONG*>(0x007C7C90), cursorHeight = *reinterpret_cast<LONG*>(0x007C7C94);
        ok = pixels && height && self->frames == static_cast<int>(width / height) && self->frames > 0 &&
             self->width == static_cast<LONG>(width / static_cast<std::uint32_t>(self->frames)) &&
             self->height == static_cast<LONG>(height) && self->hotX == static_cast<int>(cursorWidth) * hotX / 32 &&
             self->hotY == static_cast<int>(cursorHeight) * hotY / 32;
        if (ok && (bytes == 2 || bytes == 4)) bad = FirstBadMask(self, pixels, width, height, desc[4], bytes);
        else if (ok) bad = -1;
        if (locked) SurfaceSlot(locked, 32, static_cast<RECT*>(nullptr));
        ::LeaveCriticalSection(lock);
    }
    ei::verify::Check("CursorImage::Construct", true, ok && bad == -1,
                      "%s: %ux%u, frames %d of width %ld, hot spot %d,%d, surface %p, mask pixel %d", name ? name : "",
                      desc[3], desc[2], self->frames, static_cast<long>(self->width), self->hotX, self->hotY,
                      holder ? holder->surface : nullptr, bad);
    return result;
}
EI_VERIFY(0x005E4EB0, CursorImageVerify, 0, s_cursorImage);
