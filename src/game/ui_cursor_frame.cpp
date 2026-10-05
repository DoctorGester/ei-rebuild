// The software mouse cursor in the frame (005E54A0, thiscall on the cursor object 007C7BD8; called
// by the frame function 00458010 after the scene and the interface, before the present, when the
// display option's software cursor bit (the renderer's +4 bit 0) is set). The cursor's screen
// rectangle (updated by 005E52E0) is copied from the render target into the background surface;
// the cursor image (the part `from_` of it) is combined with it in the composition surface
// (CCursor::Blit: (background & mask) | image) and that is copied back to the render target;
// `drawn_` keeps the rectangle drawn. Without an image in mode 2 the background is saved from the
// primary surface instead. The new renderer (src/render) gets the image and its mask for the same
// rectangle. See docs/notes/ui.md ("Cursor") and docs/notes/renderer.md.
//
// Also the cursor's draws straight to the primary surface (ticket R6): the cursor thread 005E5690
// (redraws the cursor on the primary every 41 ms between frames), the background save 005E6630 and
// the holder blit 005E5640 they use. With the new renderer presenting ("present") the cursor is drawn
// only by the new renderer, in every frame it presents (render::Cursor above): the thread, the
// background saves and the frame draw's old blits into the render target (which the new frame
// overwrites at the present) are left out. Between frames (loading, movies) the cursor then does
// not move; the movie players hide it anyway.
#include "ui_cursor.h"
#include "texture.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr unsigned long kBltWaitAsync = 0x1000200;   // DDBLT_WAIT | DDBLT_ASYNC
constexpr int kBlt = 5;                              // IDirectDrawSurface7::Blt
constexpr int kWaitForVerticalBlank = 22;            // IDirectDraw7::WaitForVerticalBlank
constexpr DWORD kThreadPeriod = 41;                  // ms between the thread's redraws

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
// The cursor size (007C7C90 / 007C7C94): the thread's first background save.
LONG CursorWidth() { return *reinterpret_cast<LONG*>(0x007C7C90); }
LONG CursorHeight() { return *reinterpret_cast<LONG*>(0x007C7C94); }

// 00500960: the renderer object (007AF060); its +4 bit 0 is the software cursor display option.
bool SoftwareCursor() { return (*(static_cast<std::uint8_t*>(DisplayObject()) + 4) & 1) != 0; }
// 005E52E0 (thiscall): moves the cursor's rectangles to the mouse position; nonzero when shown.
__declspec(naked) char __fastcall UpdatePosition(CCursor*) EI_ORIGINAL(0x005E52E0)
// 005E5C40 (thiscall): the composition (ui_cursor.cpp, through its address).
__declspec(naked) void __fastcall CursorBlit(CCursor*, void*, CursorImage*, RECT*, RECT*) EI_ORIGINAL(0x005E5C40)
// 005E5640 (rewritten below) through its address, so its wrapper sees the calls.
__declspec(naked) long __fastcall HolderBltAt(CDXSurface*, void*, RECT*, CDXSurface*, RECT*, unsigned long, void*)
    EI_ORIGINAL(0x005E5640)

CDXSurface* RenderTarget(const CDXSurface* holder) { return holder->device->renderTarget_; }
CDXSurface* Primary(const CDXSurface* holder) { return holder->device->primary_; }

LONG Width(const RECT& r) { return r.right - r.left; }
LONG Height(const RECT& r) { return r.bottom - r.top; }
RECT Local(const RECT& r) { return RECT{0, 0, Width(r), Height(r)}; }

// The device object's IDirectDraw7 waits for the vertical blank (DDWAITVB_BLOCKBEGIN) under the
// device lock.
void WaitForVerticalBlank(CCursor* self) {
    gameapi::EnterCriticalSection(DeviceLock());
    ComCall(self->device_->ddraw_, kWaitForVerticalBlank, 1ul, static_cast<void*>(nullptr));
    gameapi::LeaveCriticalSection(DeviceLock());
}

// Blt of `source` into `target` under the device lock (no result kept).
void LockedBlt(CDXSurface* target, RECT* to, CDXSurface* source, RECT* from) {
    gameapi::EnterCriticalSection(DeviceLock());
    ComCall(target->surface, kBlt, to, source->surface, from, kBltWaitAsync, static_cast<void*>(nullptr));
    gameapi::LeaveCriticalSection(DeviceLock());
}

render::Rect ToRender(const RECT& r) { return render::Rect{r.left, r.top, r.right, r.bottom}; }

} // namespace

// 005E5640 (thiscall on the destination holder, RET 0x14): Blt from `source`'s surface (rectangles,
// flags, effects) under the device lock; Blt's result.
long __fastcall HolderBlt(CDXSurface* self, void*, RECT* to, CDXSurface* source, RECT* from, unsigned long flags, void* fx) {
    gameapi::EnterCriticalSection(DeviceLock());
    long hr = ComCall(self->surface, kBlt, to, source->surface, from, flags, fx);
    gameapi::LeaveCriticalSection(DeviceLock());
    return hr;
}
EI_REPLACE(0x005E5640, HolderBlt);

// 005E54A0.
void __fastcall CursorDrawFrame(CCursor* self) {
    if (!SoftwareCursor()) return;
    if (!self->enabled_) return;
    UpdatePosition(self);
    RECT* rect = &self->rect_;
    if (gameapi::IsRectEmpty(rect)) return;
    bool presenting = render::Presenting();
    RECT local = Local(*rect);
    if (self->imageShown_ != 0) {
        CDXSurface* under = self->under_;
        CDXSurface* screen = RenderTarget(under);
        if (!presenting && screen->surface != under->surface)
            HolderBltAt(under, nullptr, &local, screen, rect, kBltWaitAsync, nullptr);
        local = Local(*rect);
        CursorImage* image = self->image_;
        if (!presenting) CursorBlit(self, nullptr, image, &local, &self->from_);
        if (render::Active()) render::Cursor(image->holder, image->saved, ToRender(self->from_), ToRender(*rect));
        local = Local(*rect);
        CDXSurface* composed = self->target_;
        CDXSurface* target = RenderTarget(composed);
        if (!presenting && target->surface != composed->surface)
            HolderBltAt(target, nullptr, rect, composed, &local, kBltWaitAsync, nullptr);
        self->drawn_ = *rect;
        return;
    }
    if (self->mode_ != 2 || presenting) return;
    CDXSurface* under = self->under_;
    CDXSurface* primary = Primary(under);
    if (primary->surface == under->surface) return;
    LockedBlt(under, &local, primary, rect);
}
EI_REPLACE(0x005E54A0, CursorDrawFrame);

// 005E5690 (thiscall, plain RET; the cursor thread's procedure, started through 005E52A0). Saves the
// screen under the cursor once, then until `stop_` is set: every 41 ms, with the software cursor on
// and an image shown, when the cursor moved (005E52E0) it is drawn again on the primary. Apart, the
// old rectangle gets its background back, the new one's is saved and the composed cursor copied
// there (after the vertical blank); overlapping, their union is composed in the composition surface
// (the old background put back, the new one saved, the cursor combined in) and copied to the screen
// in one blit. `drawn_` becomes the new rectangle. The cursor's lock is held while it draws. At the
// end the event `stopped_` is set.
void __fastcall CursorThread(CCursor* self) {
    if (!self->enabled_) return;
    RECT* drawn = &self->drawn_;
    RECT* rect = &self->rect_;
    gameapi::EnterCriticalSection(&self->lock_);
    UpdatePosition(self);
    *drawn = *rect;
    RECT whole{0, 0, CursorWidth(), CursorHeight()};
    CDXSurface* primary = Primary(self->under_);
    if (!render::Presenting() && primary->surface != self->under_->surface)
        HolderBltAt(self->under_, nullptr, &whole, primary, drawn, kBltWaitAsync, nullptr);
    gameapi::LeaveCriticalSection(&self->lock_);
    DWORD last = gameapi::timeGetTime();
    while (!self->stop_) {
        DWORD now = gameapi::timeGetTime();
        if (now - last < kThreadPeriod) {
            gameapi::Sleep(kThreadPeriod - (now - last));
            continue;
        }
        last = now;
        if (!SoftwareCursor()) continue;
        // With "present" the new renderer draws the cursor in its frames; nothing goes to the primary.
        if (render::Presenting()) continue;
        gameapi::EnterCriticalSection(&self->lock_);
        char moved = UpdatePosition(self);
        if (self->imageShown_ == 0 || moved == 0) {
            gameapi::LeaveCriticalSection(&self->lock_);
            continue;
        }
        CursorImage* image = self->image_;
        RECT* from = &self->from_;
        CDXSurface* under = self->under_;
        CDXSurface* composed = self->target_;
        RECT overlap;
        if (!gameapi::IntersectRect(&overlap, drawn, rect)) {
            WaitForVerticalBlank(self);
            if (!gameapi::IsRectEmpty(drawn)) {
                // The old place gets its background back.
                RECT saved = Local(*drawn);
                CDXSurface* screen = Primary(under);
                if (screen->surface != under->surface) HolderBltAt(screen, nullptr, drawn, under, &saved, kBltWaitAsync, nullptr);
            }
            if (!gameapi::IsRectEmpty(rect)) {
                RECT saved = Local(*rect);
                CDXSurface* screen = Primary(under);
                if (screen->surface != under->surface) HolderBltAt(under, nullptr, &saved, screen, rect, kBltWaitAsync, nullptr);
                RECT to = Local(*rect);
                CursorBlit(self, nullptr, image, &to, from);
                RECT composedPart = Local(*rect);
                screen = Primary(composed);
                if (screen->surface != composed->surface)
                    HolderBltAt(screen, nullptr, rect, composed, &composedPart, kBltWaitAsync, nullptr);
            }
        } else {
            RECT both;
            gameapi::UnionRect(&both, drawn, rect);
            RECT bothLocal = Local(both);
            CDXSurface* screen = Primary(composed);
            if (screen->surface != composed->surface)
                HolderBltAt(composed, nullptr, &bothLocal, screen, &both, kBltWaitAsync, nullptr);
            // The old background (the image's size) back into the composition.
            LONG dx = drawn->left - both.left, dy = drawn->top - both.top;
            RECT imagePart{0, 0, image->width, image->height};
            RECT oldPlace{dx, dy, image->width + dx, image->height + dy};
            LockedBlt(composed, &oldPlace, under, &imagePart);
            // The new background from the composition.
            RECT newPlace = *rect;
            gameapi::OffsetRect(&newPlace, -both.left, -both.top);
            RECT saved = Local(*rect);
            LockedBlt(under, &saved, composed, &newPlace);
            RECT to{rect->left - both.left, rect->top - both.top, 0, 0};
            to.right = Width(*rect) + to.left;
            to.bottom = Height(*rect) + to.top;
            CursorBlit(self, nullptr, image, &to, from);
            WaitForVerticalBlank(self);
            RECT composedPart = Local(both);
            screen = Primary(composed);
            if (screen->surface != composed->surface) LockedBlt(screen, &both, composed, &composedPart);
        }
        *drawn = *rect;
        gameapi::LeaveCriticalSection(&self->lock_);
    }
    gameapi::SetEvent(self->stopped_);
}
EI_REPLACE(0x005E5690, CursorThread);

// 005E6630 (thiscall, RET 4): the part of the drawn rectangle inside `bounds_` is copied from the
// primary (`fromPrimary`) or the render target into the background surface, as the screen under the
// cursor changed (the movie players after their blits to the primary).
void __fastcall CursorSaveBackground(CCursor* self, void*, char fromPrimary) {
    if (!self->enabled_) return;
    RECT part;
    if (!gameapi::IntersectRect(&part, &self->drawn_, &self->bounds_)) return;
    // With "present" nothing draws from the background (the thread and the frame draw's old blits
    // are left out).
    if (render::Presenting()) return;
    RECT local = Local(part);
    CDXSurface* under = self->under_;
    CDXSurface* screen = fromPrimary ? Primary(under) : RenderTarget(under);
    if (screen->surface == under->surface) return;
    LockedBlt(under, &local, screen, &part);
}
EI_REPLACE(0x005E6630, CursorSaveBackground);
