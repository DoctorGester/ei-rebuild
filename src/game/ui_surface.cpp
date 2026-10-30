// The window surface's constructors and destructor (UiSurface). The holder has no DirectDraw surface and no memory:
// the surface's pixels are its canvas on the GPU (render_ui.h), made black with it, so the original's colour fill is
// left out. Making a window asks DirectDraw for nothing. The picture constructor has no caller (config/dead_code.csv).
// See docs/notes/ui.md, "Surface construction".
#include "app.h"

#include "base_manager.h"
#include "game_singletons.h"
#include "surface.h"
#include "texture.h"
#include "ui.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "render/render_ui.h"

#include <cstdint>
#include <cstring>

void UiSurface::MakeCanvas() {
    if (!holder) return;
    render::ui::Canvas(holder, width, height, ScreenFormat(holder->bitsPerPixel));
}

// The destructor: drops the contents put aside and the holder (its canvas with it) and empties the screen
// rectangle.
UiSurface::~UiSurface() {
    if (kept) render::ui::DropCanvas(this);
    kept = false;
    Surface* currentHolder = holder;
    holder = nullptr;
    if (currentHolder) currentHolder->Release();
    rects::SetEmpty(&rect);
}

UiSurface::UiSurface(Interface3D* ownerUi, int left, int top, int right, int bottom, SurfaceBlend newBlend, bool scale)
    : ui(ownerUi), blend(newBlend), rect{left, top, right, bottom} {
    if (scale) ui->ScaleRectToScreenEntry(&rect);
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;
    // (The interface's renderer is the device object.)
    holder = new Surface(ui->renderer, width, height);
    holder->AddRef();
    MakeCanvas();
}

UiSurface::UiSurface(Interface3D* ui, const Rect* rect, SurfaceBlend blend)
    : UiSurface(ui, rect->left, rect->top, rect->right, rect->bottom, blend, true) {}

void UiSurface::SetRect(int left, int top, int right, int bottom) {
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    ui->ScaleRectToScreenEntry(&rect);
}

void UiSurface::MoveTo(int x, int y) {
    Point point{x, y};
    ui->ScalePointToScreenEntry(&point);
    int rectWidth = rect.right - rect.left;
    int rectHeight = rect.bottom - rect.top;
    rect.left = point.x;
    rect.top = point.y;
    rect.right = rectWidth + point.x;
    rect.bottom = rectHeight + point.y;
}

void UiSurface::Show() {
    shown = true;
    if (kept) SwapContents(true);
}

void UiSurface::Hide(bool release) {
    shown = false;
    if (release) SwapContents(false);
}

// --- Blit --------------------------------------------------------------------------------------------

// A window surface's draw (UiSurface::Blit): when the surface exists and is shown, the part of its screen rectangle
// inside the screen is blitted onto the render target, with the source colour key background when its blend is Keyed:
// the surface's canvas is drawn (render::ui::DrawCanvas). See docs/notes/ui.md, "Surface blit".

void UiSurface::Blit() {
    if (!holder || !shown) return;
    Rect clipped{0, 0, ui->width, ui->height};
    rects::Intersect(&clipped, &clipped, &rect);
    if (rects::IsEmpty(&clipped)) return;
    const render::Rect destination{clipped.left, clipped.top, clipped.right, clipped.bottom};
    const int left = clipped.left - rect.left, top = clipped.top - rect.top;
    const render::Rect part{left, top, left + (clipped.right - clipped.left), top + (clipped.bottom - clipped.top)};
    render::ui::DrawCanvas(holder, destination, &part, blend == SurfaceBlend::Keyed, background);
}

// --- Frame -------------------------------------------------------------------------------------------

// What is drawn on an interface surface's canvas (render_ui.h) besides text: a frame (UiSurface::FrameRect; the
// tooltips draw their border with it), the whole surface filled (Clear, Fill), a filled rectangle, a pen's line and a
// pixel (the console, the carets and lines of the edit fields, chat, connection and save lists, the HUD map's dot).
// See docs/notes/ui.md, section 3.

namespace {
render::Rect ToRect(const Rect& rect) { return render::Rect{rect.left, rect.top, rect.right, rect.bottom}; }
} // namespace

void UiSurface::FrameRect(const Rect* area, std::uint32_t colour) {
    if (!holder) return;
    Rect frame;
    if (rects::IsEmpty(area)) {
        frame = {0, 0, rect.right - rect.left, rect.bottom - rect.top};
    } else {
        frame = *area;
        ui->ScaleRectToScreenEntry(&frame);           // (Rebirth-patched)
    }
    render::ui::FrameRect(holder, ToRect(frame), colour);
}

void UiSurface::Fill(std::uint32_t colour) {
    if (!holder) return;
    Rect whole = rect;
    rects::Offset(&whole, -whole.left, -whole.top);
    FillRect(whole, colour);
}

void UiSurface::Clear() { Fill(background); }

void UiSurface::FillRect(const Rect& area, std::uint32_t colour) {
    if (holder) render::ui::Fill(holder, ToRect(area), colour);
}

void UiSurface::Line(int x0, int y0, int x1, int y1, int lineWidth, std::uint32_t colour) {
    if (holder) render::ui::Line(holder, x0, y0, x1, y1, lineWidth, colour);
}

void UiSurface::Dot(int x, int y, std::uint32_t colour) {
    if (holder) render::ui::Pixel(holder, x, y, colour);
}

// --- Pixels ------------------------------------------------------------------------------------------

// The interface surfaces' pixel work (UiSurface), all on the surface's canvas on the GPU (render_ui.h): copying the
// screen into a surface, a 16-bit 5:6:5 copy of a surface at a given size (the save game's picture), greying a surface
// in place (the escape menu's background), putting a surface's pixels aside and back, and reading one pixel. See
// docs/notes/ui.md, "Surface pixel work".

void UiSurface::CaptureScreen() {
    if (holder) render::ui::CopyFrame(holder, render::Rect{rect.left, rect.top, rect.right, rect.bottom});
}

// The original stretched the surface into a new surface of the wanted size, then packed it pixel by pixel;
// the GPU does both and only the picture is read back.
std::vector<std::uint16_t> UiSurface::Rgb565Copy(int copyWidth, int copyHeight) {
    if (!holder) return {};
    std::vector<std::uint16_t> pixels(static_cast<std::size_t>(static_cast<std::uint32_t>(copyWidth) * static_cast<std::uint32_t>(copyHeight)));
    render::ui::ReadRgb565(holder, copyWidth, copyHeight, pixels.data());
    return pixels;
}

void UiSurface::Greyscale() {
    if (holder) render::ui::Greyscale(holder);
}

// The canvas goes aside under the surface's own address while it has no holder, and back onto the new one.
void UiSurface::SwapContents(bool restore) {
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
std::uint32_t UiSurface::PixelAt(int x, int y) {
    if (!holder || x < 0 || y < 0 || x >= width || y >= height) return 0;
    std::uint32_t rgb = 0;
    if (!render::ui::ReadPixel(holder, x, y, &rgb)) return 0;
    return render::ui::TopBits(rgb, ScreenFormat(holder->bitsPerPixel));
}
