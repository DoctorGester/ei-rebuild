// Surface pixels read and put back through GDI, for verify wrappers that run a text draw side by side
// (verify_ui_base.cpp, verify_ui_text_draw.cpp). The caller holds the surface's DC while it draws
// text, so the pixels go through that DC (a DirectDraw lock would collide with it).
#pragma once

#include "pixel_settle.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ei::verify::gdi {

struct GdiPixels {
    std::vector<std::uint32_t> bits;
};

// Reads the DC's top left width x height pixels into *read, or writes *write there.
inline bool CopySurfaceDC(HDC dc, int width, int height, GdiPixels* read, const GdiPixels* write) {
    if (!dc || width <= 0 || height <= 0) return false;
    HDC mem = CreateCompatibleDC(dc);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool ok = mem && bitmap;
    if (ok) {
        HGDIOBJ old = SelectObject(mem, bitmap);
        auto* pixels = static_cast<std::uint32_t*>(bits);
        std::size_t count = static_cast<std::size_t>(width) * height;
        if (write) {
            std::memcpy(pixels, write->bits.data(), count * 4);
            ok = BitBlt(dc, 0, 0, width, height, mem, 0, 0, SRCCOPY) != 0;
        } else {
            ok = BitBlt(mem, 0, 0, width, height, dc, 0, 0, SRCCOPY) != 0;
            GdiFlush();
            read->bits.assign(pixels, pixels + count);
        }
        SelectObject(mem, old);
    }
    if (bitmap) DeleteObject(bitmap);
    if (mem) DeleteDC(mem);
    return ok;
}

// The pixels that differ and their bounding box (for the mismatch line); -1 if not comparable.
struct PixelDiff {
    int count = 0, left = 0, top = 0, right = -1, bottom = -1;
};
inline PixelDiff DiffPixels(const GdiPixels& a, const GdiPixels& b, int width) {
    PixelDiff d;
    if (a.bits.size() != b.bits.size() || width <= 0) {
        d.count = -1;
        return d;
    }
    for (std::size_t i = 0; i < a.bits.size(); ++i) {
        if (a.bits[i] == b.bits[i]) continue;
        int x = static_cast<int>(i % width), y = static_cast<int>(i / width);
        if (d.count++ == 0) {
            d.left = d.right = x;
            d.top = d.bottom = y;
        }
        d.left = (std::min)(d.left, x);
        d.right = (std::max)(d.right, x);
        d.top = (std::min)(d.top, y);
        d.bottom = (std::max)(d.bottom, y);
    }
    return d;
}

// What CompareDraws found.
struct DrawComparison {
    bool compared = false;   // the pixels could be read (else nothing was compared)
    bool rerun = false;      // the first comparison differed: the original was redrawn until it settled
    bool unstable = false;   // ... and it never settled: not compared
    PixelDiff first;         // the first comparison (when rerun)
    PixelDiff diff;          // ours against the (settled) original
    SettleOutcome settle;
};

// Side by side: the pixels are recorded, `original()` draws, its pixels are recorded and the old
// ones put back, `ours()` draws, and the two are compared. Both must draw from the pixels alone (no
// state kept between calls, or the lambdas put it back). On a difference the original is redrawn
// from the recorded pixels until two draws in a row agree (an outside event on the shared desktop
// can disturb a draw, tickets X4, X5) and ours, redrawn from them too, is compared with that.
// `copy(read, write)` reads the pixels into *read or writes *write (CopySurfaceDC on some DC).
template <class Copy, class Original, class Ours>
DrawComparison CompareDrawsWith(Copy&& copy, int width, Original&& original, Ours&& ours) {
    DrawComparison out;
    GdiPixels before, expected, actual;
    bool pixels = copy(&before, nullptr);
    original();
    if (pixels) pixels = copy(&expected, nullptr) && copy(nullptr, &before);
    ours();
    if (!pixels || !copy(&actual, nullptr)) return out;
    out.compared = true;
    out.diff = DiffPixels(expected, actual, width);
    if (out.diff.count == 0) return out;
    out.rerun = true;
    out.first = out.diff;
    GdiPixels previous = expected;
    auto redraw = [&](GdiPixels* drawn) {
        if (!copy(nullptr, &before)) return false;
        original();
        return copy(drawn, nullptr);
    };
    out.settle = SettleOriginal(previous, redraw,
                                [&](const GdiPixels& a, const GdiPixels& b) { return DiffPixels(a, b, width).count; });
    GdiPixels again;
    bool ok = copy(nullptr, &before);
    ours();
    ok = ok && copy(&again, nullptr);
    out.unstable = ok && !out.settle.settled;
    out.diff = !ok ? PixelDiff{-1} : out.settle.settled ? DiffPixels(previous, again, width) : PixelDiff{};
    return out;
}

// The same on a DC the caller holds while both draw (CUiSurface::DrawTextIn).
template <class Original, class Ours>
DrawComparison CompareDraws(HDC dc, int width, int height, Original&& original, Ours&& ours) {
    return CompareDrawsWith(
        [&](GdiPixels* read, const GdiPixels* write) { return CopySurfaceDC(dc, width, height, read, write); }, width,
        original, ours);
}

} // namespace ei::verify::gdi
