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
    if (holder_) render::ui::CopyFrame(holder_, render::Rect{rect_.left, rect_.top, rect_.right, rect_.bottom});
}

// The original stretched the surface into a new surface of the wanted size, then packed it pixel by pixel;
// the GPU does both and only the picture is read back.
std::uint16_t* CUiSurface::Rgb565Copy(int width, int height) {
    if (!holder_) return nullptr;
    int count = static_cast<int>(static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height));
    auto* pixels = new std::uint16_t[static_cast<std::size_t>(count)]();
    render::ui::ReadRgb565(holder_, width, height, pixels);
    return pixels;
}

void CUiSurface::Greyscale() {
    if (holder_) render::ui::Greyscale(holder_);
}

// The canvas goes aside under the surface's own address while it has no holder, and back onto the new one.
void CUiSurface::SwapContents(bool restore) {
    if (restore) {
        if (!holder_) {
            // A new surface of the surface's size.
            holder_ = new Surface(interface_->renderer_, width_, height_);
            holder_->AddRef();
        }
        if (kept_) {
            render::ui::MoveCanvas(this, holder_);
            kept_ = false;
        } else {
            MakeCanvas();
        }
        return;
    }
    Surface* holder = holder_;
    if (!holder) return;
    render::ui::MoveCanvas(holder, this);
    kept_ = true;
    holder_ = nullptr;
    holder->Release();
}

// The pixel at (x, y) of the surface as 0x00RRGGBB, each channel's bits of the screen's format at the top of its
// byte (as the format converter gives it); 0 outside the surface or without one.
std::uint32_t CUiSurface::PixelAt(int x, int y) {
    if (!holder_ || x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
    std::uint32_t rgb = 0;
    if (!render::ui::ReadPixel(holder_, x, y, &rgb)) return 0;
    const PixelFormat screen = ScreenFormat(holder_->bitsPerPixel);
    ChannelLayout layout;
    layout.Split(screen);
    return layout.FromNative(
        render::ui::Pack(rgb, render::ui::PixelFormat{screen.bitsPerPixel, screen.redMask, screen.greenMask, screen.blueMask}));
}
