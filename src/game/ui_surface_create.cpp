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
    if (!holder) return;
    render::ui::Canvas(holder, width, height, ScreenFormat(holder->bitsPerPixel));
}

// The destructor: drops the contents put aside and the holder (its canvas with it) and empties the screen
// rectangle.
CUiSurface::~CUiSurface() {
    if (kept) render::ui::DropCanvas(this);
    kept = false;
    Surface* currentHolder = holder;
    holder = nullptr;
    if (currentHolder) currentHolder->Release();
    gameapi::SetRectEmpty(&rect);
}

CUiSurface::CUiSurface(CInterface3D* ownerUi, int left, int top, int right, int bottom, SurfaceBlend newBlend, bool scale)
    : ui(ownerUi), blend(newBlend), rect{left, top, right, bottom} {
    if (scale) ui->ScaleRectToScreenEntry(&rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    // (The interface's renderer is the device object.)
    holder = new Surface(ui->renderer, width, height);
    holder->AddRef();
    MakeCanvas();
}

CUiSurface::CUiSurface(CInterface3D* ui, const RECT* rect, SurfaceBlend blend)
    : CUiSurface(ui, rect->left, rect->top, rect->right, rect->bottom, blend, true) {}
