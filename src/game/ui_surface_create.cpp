// The window surface's constructors and destructor (CUiSurface). The holder has no DirectDraw surface, only the new
// renderer's memory (surface.cpp): the surface is already zero, so the original's colour fill is left out. Making a
// window asks DirectDraw for nothing. The picture constructor has no caller (config/dead_code.csv). See docs/notes/ui.md,
// "Surface construction".
#include "app.h"
#include "ui.h"
#include "ui_native.h"

#include "base_manager.h"
#include "surface.h"
#include "texture.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
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
    self->kept_ = nullptr;
    self->flags_ |= CUiSurface::kShown;
    self->keptSize_ = 0;
    return self;
}

void Begin(CUiSurface* self, CInterface3D* ui, int format) {
    ui_native::Forget(self);   // (a surface made again where another was)
    self->holder_ = nullptr;
    self->flags_ = static_cast<std::uint32_t>(format);
    self->background_ = 0;
    self->interface_ = ui;
}


} // namespace


// The destructor: frees the kept bytes (a total), takes the surface's height * pitch off the surface-bytes
// total, drops the holder and empties the screen rectangle.
CUiSurface::~CUiSurface() {
    ui_native::Forget(this);
    if (std::uint8_t* kept = kept_) {
        delete[] kept;
        kept_ = nullptr;
    }
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
