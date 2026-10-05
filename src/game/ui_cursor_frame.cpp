// The software mouse cursor in the frame (005E54A0, thiscall on the cursor object 007C7BD8; called
// by the frame function 00458010 after the scene and the interface, before the present, when the
// display option's software cursor bit (the renderer's +4 bit 0) is set). The cursor's screen
// rectangle (+0x30, updated by 005E52E0) is copied from the render target into the background
// surface (+0x60); the cursor image (+0x10, the part +0x40 of it) is combined with it in the
// composition surface (+0x64, CCursor::Blit: (background & mask) | image) and that is copied back
// to the render target; +0x20 keeps the rectangle drawn. Without an image (+0x0C 0) in mode 2 the
// background is saved from the primary surface instead. The new renderer (src/render) gets the
// image and its mask for the same rectangle. See docs/notes/ui.md ("Cursor") and
// docs/notes/renderer.md.
//
// Also the cursor's draws straight to the primary surface (ticket R6): the cursor thread 005E5690
// (redraws the cursor on the primary every 41 ms between frames), the background save 005E6630 and
// the holder blit 005E5640 they use. With the new renderer presenting ("present") the cursor is drawn
// only by the new renderer, in every frame it presents (render::Cursor above): the thread, the
// background saves and the frame draw's old blits into the render target (which the new frame
// overwrites at the present) are left out. Between frames (loading, movies) the cursor then does
// not move; the movie players hide it anyway.
#include "ui_cursor.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* p, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(p) + offset);
}

constexpr unsigned long kBltWaitAsync = 0x1000200;   // DDBLT_WAIT | DDBLT_ASYNC
constexpr int kBlt = 5;                              // IDirectDrawSurface7::Blt
constexpr int kWaitForVerticalBlank = 22;            // IDirectDraw7::WaitForVerticalBlank
constexpr DWORD kThreadPeriod = 41;                  // ms between the thread's redraws

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// 00500960: the renderer object (007AF060).
std::uint8_t* __cdecl Renderer() { return (std::uint8_t*)DisplayObject(); }
// 005E52E0 (thiscall): moves the cursor's rectangles to the mouse position; nonzero when shown.
__declspec(naked) char __fastcall UpdatePosition(CCursor*) EI_ORIGINAL(0x005E52E0)
// 005E5C40 (thiscall): the composition (ui_cursor.cpp, through its address).
__declspec(naked) void __fastcall CursorBlit(CCursor*, void*, CursorImage*, RECT*, RECT*) EI_ORIGINAL(0x005E5C40)
// 005E5640 (rewritten below) through its address, so its wrapper sees the calls.
__declspec(naked) long __fastcall HolderBltAt(std::uint8_t*, void*, RECT*, std::uint8_t*, RECT*, unsigned long, void*)
    EI_ORIGINAL(0x005E5640)

// A surface holder: +0x0C its device object (+0x14 the primary's holder, +0x18 the render
// target's), +0x10 the IDirectDrawSurface7.
void* Surface(std::uint8_t* holder) { return At<void*>(holder, 0x10); }
std::uint8_t* RenderTarget(std::uint8_t* holder) { return At<std::uint8_t*>(At<std::uint8_t*>(holder, 0x0C), 0x18); }
std::uint8_t* Primary(std::uint8_t* holder) { return At<std::uint8_t*>(At<std::uint8_t*>(holder, 0x0C), 0x14); }

LONG Width(const RECT& r) { return r.right - r.left; }
LONG Height(const RECT& r) { return r.bottom - r.top; }

// The cursor's +0x04 device object: its +0x0C IDirectDraw7 waits for the vertical blank
// (DDWAITVB_BLOCKBEGIN) under the device lock.
void WaitForVerticalBlank(CCursor* self) {
    std::uint8_t* device = At<std::uint8_t*>(self, 4);
    gameapi::EnterCriticalSection(DeviceLock());
    ComCall(At<void*>(device, 0x0C), kWaitForVerticalBlank, 1ul, static_cast<void*>(nullptr));
    gameapi::LeaveCriticalSection(DeviceLock());
}

// Blt of `source` into `target` under the device lock (no result kept).
void LockedBlt(std::uint8_t* target, RECT* to, std::uint8_t* source, RECT* from) {
    gameapi::EnterCriticalSection(DeviceLock());
    ComCall(Surface(target), kBlt, to, Surface(source), from, kBltWaitAsync, static_cast<void*>(nullptr));
    gameapi::LeaveCriticalSection(DeviceLock());
}

} // namespace

// 005E5640 (thiscall on the destination holder, RET 0x14): Blt from `source`'s surface (rectangles,
// flags, effects) under the device lock; Blt's result.
long __fastcall HolderBlt(std::uint8_t* self, void*, RECT* to, std::uint8_t* source, RECT* from, unsigned long flags, void* fx) {
    gameapi::EnterCriticalSection(DeviceLock());
    long hr = ComCall(Surface(self), kBlt, to, Surface(source), from, flags, fx);
    gameapi::LeaveCriticalSection(DeviceLock());
    return hr;
}
EI_REPLACE(0x005E5640, HolderBlt);

// 005E54A0.
void __fastcall CursorDrawFrame(CCursor* self) {
    if (!(At<std::uint8_t>(Renderer(), 4) & 1)) return;
    if (!At<std::uint8_t>(self, 0x91)) return;
    UpdatePosition(self);
    RECT* rect = &At<RECT>(self, 0x30);
    if (gameapi::IsRectEmpty(rect)) return;
    bool presenting = render::Presenting();
    RECT local{0, 0, rect->right - rect->left, rect->bottom - rect->top};
    if (At<int>(self, 0x0C) != 0) {
        std::uint8_t* under = self->under_;
        std::uint8_t* screen = RenderTarget(under);
        if (!presenting && Surface(screen) != Surface(under))
            HolderBltAt(under, nullptr, &local, screen, rect, kBltWaitAsync, nullptr);
        local = RECT{0, 0, rect->right - rect->left, rect->bottom - rect->top};
        CursorImage* image = At<CursorImage*>(self, 0x10);
        RECT* from = &At<RECT>(self, 0x40);
        if (!presenting) CursorBlit(self, nullptr, image, &local, from);
        if (render::Active()) {
            render::Cursor(image->holder, image->saved, render::Rect{from->left, from->top, from->right, from->bottom},
                           render::Rect{rect->left, rect->top, rect->right, rect->bottom});
        }
        local = RECT{0, 0, rect->right - rect->left, rect->bottom - rect->top};
        std::uint8_t* composed = self->target_;
        std::uint8_t* target = RenderTarget(composed);
        if (!presenting && Surface(target) != Surface(composed))
            HolderBltAt(target, nullptr, rect, composed, &local, kBltWaitAsync, nullptr);
        At<RECT>(self, 0x20) = *rect;
        return;
    }
    if (At<int>(self, 0) != 2 || presenting) return;
    std::uint8_t* under = self->under_;
    std::uint8_t* primary = Primary(under);
    if (Surface(primary) == Surface(under)) return;
    LockedBlt(under, &local, primary, rect);
}
EI_REPLACE(0x005E54A0, CursorDrawFrame);

// 005E5690 (thiscall, plain RET; the cursor thread's procedure, started through 005E52A0). Saves the
// screen under the cursor once, then until +0x9C is set: every 41 ms, with the software cursor on
// and an image shown (+0x0C), when the cursor moved (005E52E0) it is drawn again on the primary.
// Apart, the old rectangle (+0x20) gets its background back, the new one's is saved and the
// composed cursor copied there (after the vertical blank); overlapping, their union is composed in
// +0x64 (the old background put back, the new one saved, the cursor combined in) and copied to the
// screen in one blit. +0x20 becomes the new rectangle. The cursor's lock +0xA0 is held while it
// draws. At the end the event +0x1C is set.
void __fastcall CursorThread(CCursor* self) {
    if (!At<std::uint8_t>(self, 0x91)) return;
    CRITICAL_SECTION* lock = &At<CRITICAL_SECTION>(self, 0xA0);
    RECT* drawn = &At<RECT>(self, 0x20);
    RECT* rect = &At<RECT>(self, 0x30);
    gameapi::EnterCriticalSection(lock);
    UpdatePosition(self);
    *drawn = *rect;
    RECT whole{0, 0, *reinterpret_cast<LONG*>(0x007C7C90), *reinterpret_cast<LONG*>(0x007C7C94)};
    std::uint8_t* primary = Primary(self->under_);
    if (!render::Presenting() && Surface(primary) != Surface(self->under_))
        HolderBltAt(self->under_, nullptr, &whole, primary, drawn, kBltWaitAsync, nullptr);
    gameapi::LeaveCriticalSection(lock);
    DWORD last = gameapi::timeGetTime();
    while (!At<int>(self, 0x9C)) {
        DWORD now = gameapi::timeGetTime();
        if (now - last < kThreadPeriod) {
            gameapi::Sleep(kThreadPeriod - (now - last));
            continue;
        }
        last = now;
        if (!(At<std::uint8_t>(Renderer(), 4) & 1)) continue;
        // With "present" the new renderer draws the cursor in its frames; nothing goes to the primary.
        if (render::Presenting()) continue;
        gameapi::EnterCriticalSection(lock);
        char moved = UpdatePosition(self);
        if (At<int>(self, 0x0C) == 0 || moved == 0) {
            gameapi::LeaveCriticalSection(lock);
            continue;
        }
        CursorImage* image = At<CursorImage*>(self, 0x10);
        RECT* from = &At<RECT>(self, 0x40);
        RECT overlap;
        if (!gameapi::IntersectRect(&overlap, drawn, rect)) {
            WaitForVerticalBlank(self);
            if (!gameapi::IsRectEmpty(drawn)) {
                // The old place gets its background back.
                RECT saved{0, 0, Width(*drawn), Height(*drawn)};
                std::uint8_t* under = self->under_;
                std::uint8_t* screen = Primary(under);
                if (Surface(screen) != Surface(under)) HolderBltAt(screen, nullptr, drawn, under, &saved, kBltWaitAsync, nullptr);
            }
            if (!gameapi::IsRectEmpty(rect)) {
                RECT saved{0, 0, Width(*rect), Height(*rect)};
                std::uint8_t* under = self->under_;
                std::uint8_t* screen = Primary(under);
                if (Surface(screen) != Surface(under)) HolderBltAt(under, nullptr, &saved, screen, rect, kBltWaitAsync, nullptr);
                RECT to{0, 0, Width(*rect), Height(*rect)};
                CursorBlit(self, nullptr, image, &to, from);
                RECT composedPart{0, 0, Width(*rect), Height(*rect)};
                std::uint8_t* composed = self->target_;
                screen = Primary(composed);
                if (Surface(screen) != Surface(composed))
                    HolderBltAt(screen, nullptr, rect, composed, &composedPart, kBltWaitAsync, nullptr);
            }
        } else {
            RECT both;
            gameapi::UnionRect(&both, drawn, rect);
            RECT bothLocal{0, 0, Width(both), Height(both)};
            std::uint8_t* composed = self->target_;
            std::uint8_t* screen = Primary(composed);
            if (Surface(screen) != Surface(composed))
                HolderBltAt(composed, nullptr, &bothLocal, screen, &both, kBltWaitAsync, nullptr);
            // The old background (the image's size, +0x04 / +0x08) back into the composition.
            int dx = drawn->left - both.left, dy = drawn->top - both.top;
            LONG imageWidth = *reinterpret_cast<LONG*>(reinterpret_cast<std::uint8_t*>(image) + 4);
            LONG imageHeight = *reinterpret_cast<LONG*>(reinterpret_cast<std::uint8_t*>(image) + 8);
            RECT imagePart{0, 0, imageWidth, imageHeight};
            RECT oldPlace{dx, dy, imageWidth + dx, imageHeight + dy};
            std::uint8_t* under = self->under_;
            LockedBlt(composed, &oldPlace, under, &imagePart);
            // The new background from the composition.
            RECT newPlace = *rect;
            gameapi::OffsetRect(&newPlace, -both.left, -both.top);
            RECT saved{0, 0, Width(*rect), Height(*rect)};
            LockedBlt(under, &saved, composed, &newPlace);
            RECT to{rect->left - both.left, rect->top - both.top, 0, 0};
            to.right = Width(*rect) + to.left;
            to.bottom = Height(*rect) + to.top;
            CursorBlit(self, nullptr, image, &to, from);
            WaitForVerticalBlank(self);
            RECT composedPart{0, 0, Width(both), Height(both)};
            screen = Primary(composed);
            if (Surface(screen) != Surface(composed)) LockedBlt(screen, &both, composed, &composedPart);
        }
        *drawn = *rect;
        gameapi::LeaveCriticalSection(lock);
    }
    gameapi::SetEvent(At<HANDLE>(self, 0x1C));
}
EI_REPLACE(0x005E5690, CursorThread);

// 005E6630 (thiscall, RET 4): the part of the drawn rectangle (+0x20) inside +0x50 is copied from the
// primary (`fromPrimary`) or the render target into the background surface (+0x60), as the screen
// under the cursor changed (the movie players after their blits to the primary).
void __fastcall CursorSaveBackground(CCursor* self, void*, char fromPrimary) {
    if (!At<std::uint8_t>(self, 0x91)) return;
    RECT part;
    if (!gameapi::IntersectRect(&part, &At<RECT>(self, 0x20), &At<RECT>(self, 0x50))) return;
    // With "present" nothing draws from the background (the thread and the frame draw's old blits
    // are left out).
    if (render::Presenting()) return;
    RECT local{0, 0, Width(part), Height(part)};
    std::uint8_t* under = self->under_;
    std::uint8_t* screen = fromPrimary ? Primary(under) : RenderTarget(under);
    if (Surface(screen) == Surface(under)) return;
    LockedBlt(under, &local, screen, &part);
}
EI_REPLACE(0x005E6630, CursorSaveBackground);
