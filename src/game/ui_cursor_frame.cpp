// The software mouse cursor in the frame (005E54A0, thiscall on the cursor object 007C7BD8; called
// by the frame function 00458010 after the scene and the interface, before the present, when the
// display option's software cursor bit (the renderer's +4 bit 0) is set). The original copies the
// cursor's screen rectangle (updated by 005E52E0) from the render target into the background
// surface, combines the cursor image (the part `from_` of it) with it in the composition surface
// (005E5C40: (background & mask) | image) and copies that back to the render target; `drawn_`
// keeps the rectangle drawn. Ours hands the image and its mask for the same rectangle to the new
// renderer (render::Cursor), which draws it in every frame it presents. See docs/notes/ui.md
// ("Cursor") and docs/notes/renderer.md.
//
// Also the cursor's draws straight to the primary surface in the original (ticket R6): the cursor
// thread 005E5690 (redraws the cursor on the primary every 41 ms between frames) and the background
// save 005E6630. The cursor's holders have no DirectDraw surface (ticket R7): the background saves,
// the composition 005E5C40 and the holder blits 005E5640 are left out (R11a; R11b removed the two
// ports, which nothing of ours calls), and the cursor thread moves the cursor between frames by
// presenting the last frame again (render::CursorBetweenFrames, ticket R9c2); as in the original,
// not while the image is hidden (the movie players and the loading screen hide it).
#include "model.h"
#include "ui_cursor.h"
#include "texture.h"

#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr DWORD kThreadPeriod = 41;                  // ms between the thread's redraws

// 00500960: the renderer object (007AF060); its flags' bit 0 is the software cursor display option.
bool SoftwareCursor() { return (SceneRenderer()->flags_ & 1) != 0; }

render::Rect ToRender(const RECT& r) { return render::Rect{r.left, r.top, r.right, r.bottom}; }

// The cursor thread's step (R9c2): under the cursor's lock, the position updated (005E52E0) and,
// when an image is shown and the cursor moved (or its frame stepped), the last frame presented again
// with the cursor at its new rectangle (render::CursorBetweenFrames; nothing while the main thread's
// frames come often enough); the drawn rectangle follows as the original's blits made it.
void MoveBetweenFrames(CCursor* self) {
    gameapi::EnterCriticalSection(&self->lock_);
    char moved = self->UpdatePosition();
    if (self->imageShown_ != 0 && moved != 0) {
        CursorImage* image = self->image_;
        render::CursorBetweenFrames(image->holder, image->saved, ToRender(self->from_), ToRender(self->rect_));
        self->drawn_ = self->rect_;
    }
    gameapi::LeaveCriticalSection(&self->lock_);
}

} // namespace

// 005E54A0. (Without an image shown the original saves the background from the primary in mode 2:
// nothing here.)
void __fastcall CursorDrawFrame(CCursor* self) {
    if (!SoftwareCursor()) return;
    if (!self->enabled_) return;
    self->UpdatePosition();
    RECT* rect = &self->rect_;
    if (gameapi::IsRectEmpty(rect)) return;
    if (self->imageShown_ == 0) return;
    CursorImage* image = self->image_;
    render::Cursor(image->holder, image->saved, ToRender(self->from_), ToRender(*rect));
    self->drawn_ = *rect;
}
EI_REPLACE(0x005E54A0, CursorDrawFrame);

// 005E5690 (thiscall, plain RET; the cursor thread's procedure, started through 005E52A0). Takes the
// cursor's place once, then until `stop_` is set: every 41 ms, with the software cursor on, one
// MoveBetweenFrames (the original: when an image is shown and the cursor moved, its old place's
// background restored, the new one's saved and the composed cursor blitted to the primary, after the
// vertical blank). At the end the event `stopped_` is set.
void __fastcall CursorThread(CCursor* self) {
    if (!self->enabled_) return;
    gameapi::EnterCriticalSection(&self->lock_);
    self->UpdatePosition();
    self->drawn_ = self->rect_;
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
        MoveBetweenFrames(self);
    }
    gameapi::SetEvent(self->stopped_);
}
EI_REPLACE(0x005E5690, CursorThread);

// 005E52A0 (cdecl; no Ghidra function; S16b): the procedure AfxBeginThread starts (the cursor as its
// parameter): CursorThread, then exit code 0.
unsigned __cdecl CursorThreadEntry(void* cursor) {
    CursorThread(static_cast<CCursor*>(cursor));
    return 0;
}
EI_REPLACE(0x005E52A0, CursorThreadEntry);

// 005E6630 (thiscall, RET 4): in the original the part of the drawn rectangle inside `bounds_` is
// copied from the primary (`fromPrimary`) or the render target into the background surface, as the
// screen under the cursor changed (the movie players after their blits to the primary). Nothing
// draws from the background here (the thread and the frame draw's blits are left out): nothing.
void CCursor::SaveBackground(char /*fromPrimary*/) {}
EI_REPLACE_METHOD(0x005E6630, CCursor::SaveBackground);
