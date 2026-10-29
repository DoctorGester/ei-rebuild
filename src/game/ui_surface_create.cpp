// The window surface's constructors and destructor (CUiSurface). The holder has no DirectDraw surface and no memory:
// the surface's pixels are its canvas on the GPU (render_ui.h), made black with it, so the original's colour fill is
// left out. Making a window asks DirectDraw for nothing. The picture constructor has no caller (config/dead_code.csv).
// See docs/notes/ui.md, "Surface construction".
#include "app.h"
#include "ui.h"

#include "base_manager.h"
#include "surface.h"
#include "texture.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "render/render_ui.h"
#include <cstdint>

#include <windows.h>

#include <cstring>
#include "game_singletons.h"

namespace {

// Puts `made` (counted once more, may be null) into the surface's holder and drops the previous one's
// reference through the body of the reference release (the deleting destructor at zero).
void ReplaceHolder(CUiSurface* self, Surface* made) {
    Surface* previous = self->holder_;
    self->holder_ = made;
    if (made) ++made->refs_;
    if (previous) previous->Release();
}

// The common tail: rect_ holds the layout rectangle, interface_ the interface, flags_ the format.
CUiSurface* Finish(CUiSurface* self, bool scale) {
    RECT* rect = &self->rect_;
    if (scale) self->interface_->ScaleRectToScreenEntry(rect);
    self->width_ = rect->right - rect->left;
    self->height_ = rect->bottom - rect->top;
    // (The interface's renderer is the device object.)
    Surface* made = new Surface(self->interface_->renderer_, self->width_, self->height_);
    ReplaceHolder(self, made);
    self->MakeCanvas();
    self->kept_ = false;
    self->flags_ |= CUiSurface::kShown;
    return self;
}

void Begin(CUiSurface* self, CInterface3D* ui, int format) {
    self->holder_ = nullptr;
    self->flags_ = static_cast<std::uint32_t>(format);
    self->background_ = 0;
    self->interface_ = ui;
}

} // namespace

void CUiSurface::MakeCanvas() {
    if (!holder_) return;
    const PixelFormat screen = ScreenFormat(holder_->bitsPerPixel);
    render::ui::Canvas(holder_, width_, height_,
                       render::ui::PixelFormat{screen.bitsPerPixel, screen.redMask, screen.greenMask, screen.blueMask});
}

// The destructor: drops the contents put aside and the holder (its canvas with it) and empties the screen
// rectangle.
CUiSurface::~CUiSurface() {
    if (kept_) render::ui::DropCanvas(this);
    kept_ = false;
    Surface* holder = holder_;
    holder_ = nullptr;
    if (holder) holder->Release();
    gameapi::SetRectEmpty(&rect_);
}

// The scale flag is a byte.
CUiSurface::CUiSurface(CInterface3D* ui, int left, int top, int right, int bottom, int format, int flag) {
    Begin(this, ui, format);
    rect_.left = left;
    rect_.top = top;
    rect_.right = right;
    rect_.bottom = bottom;
    Finish(this, static_cast<char>(flag) != 0);
}

CUiSurface::CUiSurface(CInterface3D* ui, const RECT* rect, int format) {
    Begin(this, ui, format);
    rect_ = *rect;
    Finish(this, true);
}
