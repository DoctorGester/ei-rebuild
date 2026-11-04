#pragma once

// The interface drawn into the frame: solid rectangles and glyph runs at the point of the call, in screen pixels,
// and the frame pictures (copies of the frame kept as data). Single-threaded: the thread that draws the interface.
// See docs/notes/ui-native.md, section 3.

#include "render.h"

#include <cstdint>
#include <vector>

namespace render::text {
class TextFont;
struct TextLayout;
} // namespace render::text

namespace render::ui {

// `rect` filled with `colour` (a COLORREF), clipped to `clip`.
void Solid(const Rect& rect, std::uint32_t colour, const Rect& clip);
// A text layout (render_text.h) drawn with its rectangle's origin at `origin` in `colour` (a COLORREF): each pixel
// colour x coverage over what the frame holds there, underlines fully covered; clipped to `clip` and to the
// layout's clip rectangle. The font (and its atlas) must outlive the frame.
void Glyphs(text::TextFont& font, const text::TextLayout& layout, Point origin, std::uint32_t colour, const Rect& clip);

// GDI's pixels for a solid pen of `width` from `from` to `to` (MoveTo, LineTo) as rectangles: a one-pixel pen covers
// the pixels from the start up to the end, not including it; a two-pixel pen (round ends; measured on Windows 10, the
// console's caret) draws both end points, vertical with the column left of the line too, horizontal with the row
// above too, without the start column, and a zero-length line as one dot. Slanted lines and wider pens: a pen-wide
// square at each of Bresenham's steps (close to GDI, not exact).
std::vector<Rect> LineRects(Point from, Point to, int width);
// FrameRect's one-pixel border inside `rect`; none for an empty one.
std::vector<Rect> FrameRects(const Rect& rect);

// The frame pictures, each under a key (the surface it belongs to). CapturePicture: `source` of the frame drawn so
// far stretched to `width` x `height`, nearest pixel (x * source / size, as DirectDraw's Blt picked); between frames
// the last frame. DrawPicture: the picture over `destination`, stretched, greyed ((2 r + 4 g + b) / 16) with `grey`.
void CapturePicture(const void* key, const Rect& source, int width, int height);
bool HasPicture(const void* key);
void DrawPicture(const void* key, const Rect& destination, bool grey);
void DropPicture(const void* key);
// The picture stretched to `width` x `height` (each pixel the one under its centre, (2 x + 1) * size / (2 width)) as
// 5:6:5 pixels (each channel's top bits), rows from the top: the save's picture (shot.sav keeps 5:6:5).
bool ReadPictureRgb565(const void* key, int width, int height, std::uint16_t* out);
// The picture's pixel `at` as 0x00RRGGBB; false outside it or without one.
bool ReadPicturePixel(const void* key, Point at, std::uint32_t* rgb);

} // namespace render::ui
