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

CUiSurface::CUiSurface(CInterface3D* ui, int left, int top, int right, int bottom, int format, bool scale)
    : interface_(ui), flags_(static_cast<std::uint32_t>(format)), rect_{left, top, right, bottom} {
    if (scale) interface_->ScaleRectToScreenEntry(&rect_);
    width_ = rect_.right - rect_.left;
    height_ = rect_.bottom - rect_.top;
    // (The interface's renderer is the device object.)
    holder_ = new Surface(interface_->renderer_, width_, height_);
    ++holder_->refs_;
    MakeCanvas();
    flags_ |= kShown;
}

CUiSurface::CUiSurface(CInterface3D* ui, const RECT* rect, int format)
    : CUiSurface(ui, rect->left, rect->top, rect->right, rect->bottom, format, true) {}
