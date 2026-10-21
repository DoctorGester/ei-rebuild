#pragma once

// The interface's 2D canvases (ticket N1; docs/notes/ui-native.md, section 3): a window surface's
// pixels kept on the GPU in a render target, drawn into by the commands GDI draws with today (fills,
// frames, lines, pixels, glyph runs) and by the surface pixel code (the screen copy, greying), read
// back into the surface's memory for the readers, and blitted onto the frame as SurfaceBlit draws a
// surface. No GDI here.
//
// A canvas belongs to a surface holder (the CDXSurface of a CUiSurface) and holds what the surface's
// memory would: every channel cut to the format's bits and widened again (render_textures.cpp's
// Expand), so 16-bit surfaces quantise as GDI does and the colour key compares exactly.
//
// Commands are queued in call order and drawn on the GPU before the frame's next pass (the hoisting:
// drawing before a canvas's first blit of the frame lands before that blit). Drawing into a canvas
// already blitted in the frame and not drawn yet first draws the frame so far (a pass split), so the
// blit shows the canvas as it was. Single-threaded: the thread that draws the interface.

#include "render.h"

#include <cstdint>
#include <vector>

namespace render::text {
class TextFont;
struct TextLayout;
} // namespace render::text

namespace render::ui {

// A colour as GDI takes it (COLORREF, 0x00BBGGRR).
using Colour = std::uint32_t;

// A surface's pixel format (SurfaceMemory's): 16 or 32 bits, the colour masks.
struct PixelFormat {
    int bitsPerPixel = 32;
    std::uint32_t redMask = 0xFF0000, greenMask = 0xFF00, blueMask = 0xFF;
};

// `colour` as a surface of `format` keeps it (each channel's top bits) widened back to 8 bits a
// channel, as 0x00RRGGBB: what the canvas holds and the blit shows.
std::uint32_t Quantise(Colour colour, const PixelFormat& format);
// A pixel of `format` (a colour key) widened to 0x00RRGGBB; 0x00RRGGBB to a pixel of `format` (the
// channels' top bits, as CopyFrameTo packs).
std::uint32_t Widen(std::uint32_t pixel, const PixelFormat& format);
std::uint32_t Pack(std::uint32_t rgb, const PixelFormat& format);

// The canvas of `holder`: made (or made again at another size or format) all black, as a new surface's
// memory is. False for a size outside 1..8192.
bool Canvas(const void* holder, int width, int height, const PixelFormat& format);
bool HasCanvas(const void* holder);
// The holder is gone (HolderGone): its canvas and queued commands are dropped.
void DropCanvas(const void* holder);

// Drawing, in canvas pixels; each is clipped to the canvas, and does nothing without one.
// FillSolidRect: [left, right) x [top, bottom).
void Fill(const void* holder, const Rect& rect, Colour colour);
// FrameRect with a solid brush: the rectangle's one-pixel border inside it.
void FrameRect(const void* holder, const Rect& rect, Colour colour);
// MoveTo (x0, y0), LineTo (x1, y1) with a solid pen of `width`. GDI's pixels for horizontal and vertical
// lines 1 px wide (the end point not drawn) and 2 px wide (the game's pens); wider pens and slanted lines
// are close to GDI, not exact (a band around the line; Bresenham's steps).
void Line(const void* holder, int x0, int y0, int x1, int y1, int width, Colour colour);
// SetPixel.
void Pixel(const void* holder, int x, int y, Colour colour);
// A text layout (render_text.h, its rectangle in canvas pixels) drawn in `colour`: each pixel becomes
// colour x coverage + what was there x (1 - coverage), overlapping glyphs keeping the larger coverage,
// underlines fully covered; clipped to the layout's clip rectangle. Over black this is
// text::ComposeAgainstBlack. The font (and its atlas) must outlive the canvas's queued commands.
void GlyphRun(const void* holder, text::TextFont& font, const text::TextLayout& layout, Colour colour);
// 005FD3D0's screen copy: `source` of the frame drawn so far stretched over the whole canvas, nearest
// pixel (CopyFrameTo's rule), quantised to the format.
void CopyFrame(const void* holder, const Rect& source);
// 005FE440's greying, on the format's bits.
void Greyscale(const void* holder);

// The canvas's pixels after every queued command, as 0x00RRGGBB rows top-down (width x height); false
// without a canvas or when the GPU is not there.
bool ReadPixels(const void* holder, std::vector<std::uint32_t>* rgb, int* width = nullptr, int* height = nullptr);
// ReadPixels into the holder's SurfaceMemory (of the canvas's size) in its format, marked written: for
// the code that reads surface pixels (PixelAt, Rgb565Copy).
bool ReadBack(const void* holder);

// Draws the canvas onto the frame at this point of the frame's draws, as SurfaceBlit draws a surface:
// `source` (null: all of it) onto `destination`, stretched when the sizes differ, and with `keyed`
// leaving out pixels equal to `key` (a pixel of the canvas's format).
void DrawCanvas(const void* holder, const Rect& destination, const Rect* source, bool keyed, std::uint32_t key);

} // namespace render::ui
