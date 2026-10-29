#pragma once

// The interface's 2D canvases: a window surface's pixels, kept only in a GPU render target. Each canvas keeps the
// commands drawn on it since it was last covered whole (after a screen copy or a long run of commands, its pixels
// read back instead) and draws them again on a new device (a lost one).
// Single-threaded. See docs/notes/ui-native.md, section 3 and "Source file overviews".

#include "render.h"

#include <cstdint>
#include <vector>

namespace render::text {
class TextFont;
struct TextLayout;
} // namespace render::text

namespace render::ui {

// 0x00RRGGBB to a pixel of `format` (the channels' top bits).
std::uint32_t Pack(std::uint32_t rgb, const PixelFormat& format);
// 0x00RRGGBB with each channel cut to its top bits in `format` (the low bits 0): what a pixel of the format keeps,
// each channel at the top of its byte.
std::uint32_t TopBits(std::uint32_t rgb, const PixelFormat& format);

// The canvas of `holder`: made (or made again at another size or format) all black, as the original's new
// surface was. False for a size outside 1..8192.
bool Canvas(const void* holder, int width, int height, const PixelFormat& format);
bool HasCanvas(const void* holder);
// The holder is gone (HolderGone): its canvas and queued commands are dropped.
void DropCanvas(const void* holder);
// The canvas (and its queued commands) moved from the key `from` to `to`: SwapContents puts a surface's canvas
// aside while the surface has no holder and back onto its new one. A blit of it the frame
// has not drawn yet is drawn first. A canvas `to` had is dropped.
void MoveCanvas(const void* from, const void* to);

// Drawing, in canvas pixels; each is clipped to the canvas, and does nothing without one.
// FillSolidRect: [left, right) x [top, bottom).
void Fill(const void* holder, const Rect& rect, std::uint32_t colour);
// FrameRect with a solid brush: the rectangle's one-pixel border inside it.
void FrameRect(const void* holder, const Rect& rect, std::uint32_t colour);
// MoveTo (x0, y0), LineTo (x1, y1) with a solid pen of `width`. GDI's pixels for horizontal and vertical
// lines 1 px wide (the end point not drawn) and 2 px wide (the game's pens); wider pens and slanted lines
// are close to GDI, not exact (a band around the line; Bresenham's steps).
void Line(const void* holder, int x0, int y0, int x1, int y1, int width, std::uint32_t colour);
void Pixel(const void* holder, int x, int y, std::uint32_t colour);
// A text layout (render_text.h, its rectangle in canvas pixels) drawn in `colour`: each pixel becomes
// colour x coverage + what was there x (1 - coverage), overlapping glyphs keeping the larger coverage,
// underlines fully covered; clipped to the layout's clip rectangle. Over black this is
// text::ComposeAgainstBlack. The font (and its atlas) must outlive the canvas's queued commands.
void GlyphRun(const void* holder, text::TextFont& font, const text::TextLayout& layout, std::uint32_t colour);
// The screen copy: `source` of the frame drawn so far stretched over the whole canvas, nearest
// pixel (CopyFrameTo's rule), quantised to the format.
void CopyFrame(const void* holder, const Rect& source);
// The greying, on the format's bits.
void Greyscale(const void* holder);

// The canvas's pixels after every queued command, as 0x00RRGGBB rows top-down (width x height); false
// without a canvas or when the GPU is not there.
bool ReadPixels(const void* holder, std::vector<std::uint32_t>* rgb, int* width = nullptr, int* height = nullptr);
// The pixel (x, y) of the canvas after every queued command, as 0x00RRGGBB; false outside it, without one
// or when the GPU is not there.
bool ReadPixel(const void* holder, int x, int y, std::uint32_t* rgb);
// The canvas after every queued command stretched to `width` x `height` on the GPU (each pixel the canvas
// pixel under its centre, (2 x + 1) * canvas width / (2 width)) as 5:6:5 pixels (each channel's top bits),
// rows from the top, read back into `out` (width x height): the save's picture.
bool ReadRgb565(const void* holder, int width, int height, std::uint16_t* out);

// Draws the canvas onto the frame at this point of the frame's draws (the window surface's blit):
// `source` (null: all of it) onto `destination`, stretched when the sizes differ, and with `keyed`
// leaving out pixels equal to `key` (a pixel of the canvas's format).
void DrawCanvas(const void* holder, const Rect& destination, const Rect* source, bool keyed, std::uint32_t key);

// `rgb` (0x00RRGGBB rows from the top, `pitch` pixels apart) written over `destination` of the canvas
// (clipped to it), quantised to the format: a picture drawn on the CPU (ei_ui_check's; the game draws none).
void Image(const void* holder, const Rect& destination, const std::uint32_t* rgb, int pitch);

} // namespace render::ui
