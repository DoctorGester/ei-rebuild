// The interface surfaces drawn on GPU canvases (ticket P1): see ui_native.h. The native surfaces are kept in
// a side map keyed by the surface (CUiSurface is the game's 0x38 bytes; no room for a flag), their canvases
// by the surface's holder (render_ui.h), so a canvas goes with its holder (HolderGone) and SwapContents,
// which swaps the holder out and makes a new one, makes it again (Restored).
#include "ui_native.h"
#include "ui.h"

#include "render/render.h"
#include "render/render_ui.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ui_native {

namespace {

namespace canvas = render::ui;

struct NativeSurface {
    const char* family = nullptr;
    bool behind = false;   // the canvas holds drawing the memory does not have (uinative)
};
std::unordered_map<const CUiSurface*, NativeSurface> g_surfaces;

NativeSurface* Find(const CUiSurface* surface) {
    if (g_surfaces.empty()) return nullptr;
    auto it = g_surfaces.find(surface);
    return it != g_surfaces.end() ? &it->second : nullptr;
}

const render::SurfaceMemory* MemoryOf(const CUiSurface* surface) {
    const render::SurfaceMemory* memory = surface->holder_ ? render::SurfaceMemoryOf(surface->holder_) : nullptr;
    if (!memory || !memory->bits || (memory->bitsPerPixel != 16 && memory->bitsPerPixel != 32)) return nullptr;
    return memory;
}

canvas::PixelFormat FormatOf(const render::SurfaceMemory& memory) {
    return canvas::PixelFormat{memory.bitsPerPixel, memory.redMask, memory.greenMask, memory.blueMask};
}

render::Rect ToRect(const RECT& rect) { return render::Rect{rect.left, rect.top, rect.right, rect.bottom}; }

// The canvas of the surface's holder, of its memory's size and format (black when new, as the memory is).
bool MakeCanvas(const CUiSurface* surface) {
    const render::SurfaceMemory* memory = MemoryOf(surface);
    return memory && canvas::Canvas(surface->holder_, memory->width, memory->height, FormatOf(*memory));
}

// `rect` of the surface's memory (clipped to it) written onto its canvas.
void CopyMemory(const CUiSurface* surface, const RECT& rect) {
    const render::SurfaceMemory* memory = MemoryOf(surface);
    if (!memory) return;
    const int left = std::max<int>(rect.left, 0), top = std::max<int>(rect.top, 0);
    const int right = std::min<int>(rect.right, memory->width), bottom = std::min<int>(rect.bottom, memory->height);
    if (right <= left || bottom <= top) return;
    GdiFlush();   // GDI's drawing still queued for the memory
    const canvas::PixelFormat format = FormatOf(*memory);
    const int width = right - left;
    std::vector<std::uint32_t> rgb(static_cast<std::size_t>(width) * static_cast<std::size_t>(bottom - top));
    for (int y = top; y < bottom; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(memory->bits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(memory->pitch);
        std::uint32_t* out = rgb.data() + static_cast<std::size_t>(y - top) * static_cast<std::size_t>(width);
        for (int x = left; x < right; ++x) {
            const std::uint32_t pixel = memory->bitsPerPixel == 16 ? reinterpret_cast<const std::uint16_t*>(row)[x]
                                                                   : reinterpret_cast<const std::uint32_t*>(row)[x];
            out[x - left] = canvas::Widen(pixel, format);
        }
    }
    canvas::Image(surface->holder_, render::Rect{left, top, right, bottom}, rgb.data(), width);
}

bool CanvasOnly() { return canvas::CurrentMode() == canvas::Mode::Native; }

} // namespace

void Adopt(CUiSurface* surface, const char* family) {
    if (!surface || canvas::CurrentMode() == canvas::Mode::Cpu || !canvas::Listed(family)) return;
    if (MakeCanvas(surface)) g_surfaces[surface] = NativeSurface{family, false};
}

void Forget(const CUiSurface* surface) {
    if (!g_surfaces.empty()) g_surfaces.erase(surface);
}

bool IsNative(const CUiSurface* surface) { return Find(surface) && surface->holder_ && canvas::HasCanvas(surface->holder_); }

void Sync(const CUiSurface* surface) {
    NativeSurface* native = Find(surface);
    if (!native || !native->behind) return;
    native->behind = false;
    if (IsNative(surface)) canvas::ReadBack(surface->holder_);
}

void Fill(const CUiSurface* surface, const RECT& rect, COLORREF colour) {
    if (IsNative(surface)) canvas::Fill(surface->holder_, ToRect(rect), colour);
}

void FrameRect(const CUiSurface* surface, const RECT& rect, COLORREF colour) {
    if (IsNative(surface)) canvas::FrameRect(surface->holder_, ToRect(rect), colour);
}

void TextDrawn(const CUiSurface* surface, const RECT& rect, bool clipped) {
    if (!IsNative(surface)) return;
    CopyMemory(surface, clipped ? rect : RECT{0, 0, surface->width_, surface->height_});
}

bool Capture(const CUiSurface* surface, const RECT& source) {
    if (!IsNative(surface)) return true;
    canvas::CopyFrame(surface->holder_, ToRect(source));
    if (!CanvasOnly()) return true;
    Find(surface)->behind = true;
    return false;
}

bool Greyscale(const CUiSurface* surface) {
    if (!IsNative(surface)) return true;
    canvas::Greyscale(surface->holder_);
    return !CanvasOnly() || !Find(surface)->behind;
}

void Restored(CUiSurface* surface) {
    NativeSurface* native = Find(surface);
    if (!native) return;
    native->behind = false;
    if (MakeCanvas(surface)) CopyMemory(surface, RECT{0, 0, surface->width_, surface->height_});
}

} // namespace ui_native
