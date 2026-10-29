// The interface surfaces' pixel work (CUiSurface), all on the surface's canvas on the GPU (render_ui.h): copying the
// screen into a surface, a 16-bit 5:6:5 copy of a surface at a given size (the save game's picture), greying a surface
// in place (the escape menu's background), putting a surface's pixels aside and back, and reading one pixel. See
// docs/notes/ui.md, "Surface pixel work".
#include "app.h"
#include "ui.h"

#include "surface.h"
#include "texture.h"

#include "render/render_ui.h"

#include <cstdint>

void CUiSurface::CaptureScreen() {
    if (holder) render::ui::CopyFrame(holder, render::Rect{rect.left, rect.top, rect.right, rect.bottom});
}

// The original stretched the surface into a new surface of the wanted size, then packed it pixel by pixel;
// the GPU does both and only the picture is read back.
std::vector<std::uint16_t> CUiSurface::Rgb565Copy(int copyWidth, int copyHeight) {
    if (!holder) return {};
    std::vector<std::uint16_t> pixels(static_cast<std::size_t>(static_cast<std::uint32_t>(copyWidth) * static_cast<std::uint32_t>(copyHeight)));
    render::ui::ReadRgb565(holder, copyWidth, copyHeight, pixels.data());
    return pixels;
}

void CUiSurface::Greyscale() {
    if (holder) render::ui::Greyscale(holder);
}

// The canvas goes aside under the surface's own address while it has no holder, and back onto the new one.
void CUiSurface::SwapContents(bool restore) {
    if (restore) {
        if (!holder) {
            // A new surface of the surface's size.
            holder = new Surface(ui->renderer, width, height);
            holder->AddRef();
        }
        if (kept) {
            render::ui::MoveCanvas(this, holder);
            kept = false;
        } else {
            MakeCanvas();
        }
        return;
    }
    Surface* currentHolder = holder;
    if (!currentHolder) return;
    render::ui::MoveCanvas(currentHolder, this);
    kept = true;
    holder = nullptr;
    currentHolder->Release();
}

// The pixel at (x, y) of the surface as 0x00RRGGBB, each channel's bits of the screen's format at the top of its
// byte, the low bits 0; 0 outside the surface or without one.
std::uint32_t CUiSurface::PixelAt(int x, int y) {
    if (!holder || x < 0 || y < 0 || x >= width || y >= height) return 0;
    std::uint32_t rgb = 0;
    if (!render::ui::ReadPixel(holder, x, y, &rgb)) return 0;
    return render::ui::TopBits(rgb, ScreenFormat(holder->bitsPerPixel));
}
