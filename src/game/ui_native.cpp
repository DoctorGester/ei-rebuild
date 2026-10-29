// The interface surfaces drawn on GPU canvases (ticket P1): see ui_native.h. The native surfaces are kept in
// a side map keyed by the surface, their canvases by the surface's holder (render_ui.h), so a canvas goes
// with its holder (HolderGone) and SwapContents, which swaps the holder out and makes a new one, makes it
// again (Restored) or, for a canvas newer than the memory, puts it aside under the surface's own address
// and back (PutAside, TakeBack).
#include "ui_native.h"
#include "ui.h"

#include "system_text.h"

#include "render/render.h"
#include "render/render_text.h"
#include "render/render_ui.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ui_native {

namespace {

namespace canvas = render::ui;
namespace glyphs = render::text;

constexpr COLORREF kShadowColour = 0x080808;   // DrawTextIn's shadow, one pixel lower and to the right

struct NativeSurface {
    const char* family = nullptr;
    bool behind = false;   // the canvas holds drawing the memory does not have (uinative, the screen copies)
    bool aside = false;    // the canvas is kept under the surface's address while it has no holder (PutAside)
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
void MemoryToCanvas(const CUiSurface* surface, const RECT& rect) {
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

// The surface's canvas made its only copy (render::ui::CanvasOnly), the memory behind it from now on: made
// first when the surface has none, with the memory's pixels on it when `keep` (a screen copy covers it whole).
NativeSurface* GpuOnly(const CUiSurface* surface, bool keep) {
    if (!IsNative(surface)) {
        if (!MakeCanvas(surface)) return nullptr;
        if (keep) MemoryToCanvas(surface, RECT{0, 0, surface->width_, surface->height_});
    }
    NativeSurface& native = g_surfaces[surface];
    if (!native.family) native.family = "screen copy";
    native.behind = true;
    canvas::CanvasOnly(surface->holder_);
    return &native;
}

// `text` (UTF-8, else ANSI-code-page bytes, as the DrawTextA route takes it: system_text.h) as code points.
std::u32string CodePoints(std::string_view text) {
    const std::wstring wide = ::text::Wide(text);
    std::u32string out;
    out.reserve(wide.size());
    for (std::size_t i = 0; i < wide.size(); ++i) {
        const char32_t unit = wide[i];
        if (unit >= 0xD800 && unit < 0xDC00 && i + 1 < wide.size() && wide[i + 1] >= 0xDC00 && wide[i + 1] < 0xE000) {
            out.push_back(0x10000 + ((unit - 0xD800) << 10) + (static_cast<char32_t>(wide[i + 1]) - 0xDC00));
            ++i;
        } else {
            out.push_back(unit);
        }
    }
    return out;
}

glyphs::Rect ToTextRect(const RECT& rect) { return glyphs::Rect{rect.left, rect.top, rect.right, rect.bottom}; }

// The layout of `text` in `rect` drawn on the surface's canvas in `colour` (nothing drawn for DT_CALCRECT);
// DrawText's value into `*height`.
void DrawRun(const CUiSurface* surface, glyphs::TextFont& font, std::u32string_view text, const RECT& rect, UINT format,
             COLORREF colour, int* height) {
    glyphs::TextLayout layout;
    if (!glyphs::Layout(font, text, ToTextRect(rect), format, layout)) return;
    if (!(format & DT_CALCRECT)) canvas::GlyphRun(surface->holder_, font, layout, colour);
    if (height) *height = layout.result;
}

} // namespace

void Adopt(CUiSurface* surface, const char* family) {
    if (!surface || canvas::CurrentMode() == canvas::Mode::Cpu || !canvas::Listed(family)) return;
    if (MakeCanvas(surface)) g_surfaces[surface] = NativeSurface{family, false};
}

void (*g_goneObserver)(const CUiSurface* surface) = nullptr;

void Forget(const CUiSurface* surface) {
    NativeSurface* native = Find(surface);
    if (!native) return;
    if (g_goneObserver && IsNative(surface)) g_goneObserver(surface);
    if (native->aside) canvas::DropCanvas(surface);
    g_surfaces.erase(surface);
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

void Line(const CUiSurface* surface, int x0, int y0, int x1, int y1, int width, COLORREF colour) {
    if (IsNative(surface)) canvas::Line(surface->holder_, x0, y0, x1, y1, width, colour);
}

TextPath Text(const CUiSurface* surface, HFONT font, std::string_view text, const RECT& rect, UINT format,
              COLORREF colour, bool shadow, int* height) {
    NativeSurface* native = Find(surface);
    if (!native || !IsNative(surface)) return TextPath::Gdi;
    glyphs::TextFont* stand = TextFontOf(font);
    if (!stand || (format & ~glyphs::kSupportedFormats)) {
        Sync(surface);   // GDI draws into the memory and TextDrawn copies it: the memory up to date first
        return TextPath::Gdi;
    }
    const std::u32string points = CodePoints(text);
    if (shadow) {
        RECT shifted = rect;
        ::OffsetRect(&shifted, 1, 1);
        DrawRun(surface, *stand, points, shifted, format, kShadowColour, nullptr);
    }
    DrawRun(surface, *stand, points, rect, format, colour, height);
    // The memory takes GDI's text too while the comparison reads it (uiboth) and it is not behind anyway.
    if (canvas::CurrentMode() == canvas::Mode::Both && !native->behind) return TextPath::AlsoGdi;
    if (!(format & DT_CALCRECT)) native->behind = true;
    return TextPath::Canvas;
}

void TextDrawn(const CUiSurface* surface, const RECT& rect, bool clipped) {
    if (!IsNative(surface)) return;
    MemoryToCanvas(surface, clipped ? rect : RECT{0, 0, surface->width_, surface->height_});
}

void Capture(const CUiSurface* surface, const RECT& source) {
    if (GpuOnly(surface, false)) canvas::CopyFrame(surface->holder_, ToRect(source));
}

void Greyscale(const CUiSurface* surface) {
    if (GpuOnly(surface, true)) canvas::Greyscale(surface->holder_);
}

bool ReadRgb565(const CUiSurface* surface, int width, int height, std::uint16_t* out) {
    return GpuOnly(surface, true) && canvas::ReadRgb565(surface->holder_, width, height, out);
}

bool PixelOnGpu(const CUiSurface* surface, int x, int y, std::uint32_t* pixel) {
    NativeSurface* native = Find(surface);
    if (!native || !native->behind || !IsNative(surface)) return false;
    const render::SurfaceMemory* memory = MemoryOf(surface);
    std::uint32_t rgb = 0;
    if (!memory || !canvas::ReadPixel(surface->holder_, x, y, &rgb)) return false;
    *pixel = canvas::Pack(rgb, FormatOf(*memory)) | memory->alphaMask;
    return true;
}

bool PutAside(const CUiSurface* surface) {
    NativeSurface* native = Find(surface);
    if (!native || !native->behind || !IsNative(surface)) return false;
    canvas::MoveCanvas(surface->holder_, surface);
    native->aside = true;
    return true;
}

bool TakeBack(const CUiSurface* surface) {
    NativeSurface* native = Find(surface);
    if (!native || !native->aside) return false;
    native->aside = false;
    if (!surface->holder_) {
        canvas::DropCanvas(surface);
        return false;
    }
    canvas::MoveCanvas(surface, surface->holder_);
    return true;
}

void Restored(CUiSurface* surface) {
    NativeSurface* native = Find(surface);
    if (!native) return;
    native->behind = false;
    if (MakeCanvas(surface)) MemoryToCanvas(surface, RECT{0, 0, surface->width_, surface->height_});
}

} // namespace ui_native
