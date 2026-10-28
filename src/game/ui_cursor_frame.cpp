// The software mouse cursor in the frame: the image and its mask for the cursor's rectangle go to the new renderer
// (render::Cursor), which draws it in every frame it presents; the cursor thread moves it between frames by presenting
// the last frame again (render::CursorBetweenFrames), not while the image is hidden. The DirectDraw background saves
// and composition are left out. See docs/notes/ui.md, "Cursor", and docs/notes/renderer.md.
#include "model.h"
#include "ui_cursor.h"
#include "texture.h"

#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr DWORD kThreadPeriod = 41;                  // ms between the thread's redraws

// The renderer object; its flags' bit 0 is the software cursor display option.
bool SoftwareCursor() { return (SceneRenderer()->flags_ & 1) != 0; }

render::Rect ToRender(const RECT& r) { return render::Rect{r.left, r.top, r.right, r.bottom}; }

// The cursor thread's step: under the cursor's lock, the position updated and,
// when an image is shown and the cursor moved (or its frame stepped), the last frame presented again
// with the cursor at its new rectangle (render::CursorBetweenFrames; nothing while the main thread's
// frames come often enough); the drawn rectangle follows as the DirectDraw blits made it.
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

// (Without an image shown, DirectDraw saves the background from the primary in mode 2: nothing here.)
void CursorDrawFrame(CCursor* self) {
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

// Takes the cursor's place once, then until `stop_` is set: every 41 ms, with the software cursor on, one
// MoveBetweenFrames (DirectDraw: when an image is shown and the cursor moved, its old place's background
// restored, the new one's saved and the composed cursor blitted to the primary, after the vertical blank). At
// the end the event `stopped_` is set.
void CursorThread(CCursor* self) {
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

// The procedure AfxBeginThread starts (the cursor as its
// parameter): CursorThread, then exit code 0.
unsigned __cdecl CursorThreadEntry(void* cursor) {
    CursorThread(static_cast<CCursor*>(cursor));
    return 0;
}

// The part of the drawn rectangle inside `bounds_` is
// copied from the primary (`fromPrimary`) or the render target into the background surface, as the
// screen under the cursor changed (the movie players after their blits to the primary). Nothing
// draws from the background here (the thread and the frame draw's blits are left out): nothing.
void CCursor::SaveBackground(char /*fromPrimary*/) {}
