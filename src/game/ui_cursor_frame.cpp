// The software mouse cursor in the frame: the image and its mask for the cursor's rectangle go to the new renderer
// (render::Cursor), which draws it in every frame it presents; the cursor thread moves it between frames by presenting
// the last frame again (render::CursorBetweenFrames), not while the image is hidden. The DirectDraw background saves
// and composition are left out. See docs/notes/ui.md, "Cursor", and docs/notes/renderer.md.
#include "model.h"
#include "ui_cursor.h"
#include "texture.h"
#include "game_singletons.h"

#include "lib/gameapi.h"
#include "render/render.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr DWORD kThreadPeriod = 41;                  // ms between the thread's redraws

// The renderer object; its flags' bit 0 is the software cursor display option.
bool SoftwareCursor() { return (g_sceneRenderer.flags & 1) != 0; }

render::Rect ToRender(const RECT& r) { return render::Rect{r.left, r.top, r.right, r.bottom}; }

// The cursor thread's step: under the cursor's lock, the position updated and,
// when an image is shown and the cursor moved (or its frame stepped), the last frame presented again
// with the cursor at its new rectangle (render::CursorBetweenFrames; nothing while the main thread's
// frames come often enough); the drawn rectangle follows as the DirectDraw blits made it.
void MoveBetweenFrames(CCursor* self) {
    gameapi::EnterCriticalSection(&self->lock);
    const bool moved = self->UpdatePosition();
    if (self->imageShown && moved) {
        CursorImage* image = self->image;
        render::CursorBetweenFrames(image->holder, image->saved.data(), ToRender(self->from), ToRender(self->rect));
        self->drawn = self->rect;
    }
    gameapi::LeaveCriticalSection(&self->lock);
}

} // namespace

// (Without an image shown, DirectDraw saves the background from the primary in mode 2: nothing here.)
void CursorDrawFrame(CCursor* self) {
    if (!SoftwareCursor()) return;
    if (!self->enabled) return;
    self->UpdatePosition();
    RECT* rect = &self->rect;
    if (gameapi::IsRectEmpty(rect)) return;
    if (!self->imageShown) return;
    CursorImage* image = self->image;
    render::Cursor(image->holder, image->saved.data(), ToRender(self->from), ToRender(*rect));
    self->drawn = *rect;
}

// Takes the cursor's place once, then until `stop` is set: every 41 ms, with the software cursor on, one
// MoveBetweenFrames (DirectDraw: when an image is shown and the cursor moved, its old place's background
// restored, the new one's saved and the composed cursor blitted to the primary, after the vertical blank). At
// the end the event `stopped` is set.
void CursorThread(CCursor* self) {
    if (!self->enabled) return;
    gameapi::EnterCriticalSection(&self->lock);
    self->UpdatePosition();
    self->drawn = self->rect;
    gameapi::LeaveCriticalSection(&self->lock);
    DWORD last = gameapi::timeGetTime();
    while (!self->stop) {
        DWORD now = gameapi::timeGetTime();
        if (now - last < kThreadPeriod) {
            gameapi::Sleep(kThreadPeriod - (now - last));
            continue;
        }
        last = now;
        if (!SoftwareCursor()) continue;
        MoveBetweenFrames(self);
    }
    gameapi::SetEvent(self->stopped);
}

// The procedure lib::StartThread starts (the cursor as its parameter): CursorThread, then exit code 0.
unsigned __stdcall CursorThreadEntry(void* cursor) {
    CursorThread(static_cast<CCursor*>(cursor));
    return 0;
}

