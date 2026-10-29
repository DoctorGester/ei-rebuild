#pragma once

// The interface surfaces drawn on GPU canvases (docs/notes/ui-native.md, "P1"). A surface of a family EI_RENDER's "uiboth" /
// "uinative" lists gets a canvas when it is made (Adopt); the surface functions record their drawing on it beside GDI and
// the CPU code, and the memory stays the source of truth. The screen copies are different: a surface the screen is
// copied into (the backdrops, the global map's pick picture, the quick save's copy) keeps its pixels on the GPU only, in
// any mode: the copy, its greying, the save's 5:6:5 picture and its pixel reads are done there (Capture, Greyscale,
// ReadRgb565, PixelOnGpu). Otherwise, without a "ui" word in EI_RENDER, nothing is adopted and every function here does
// nothing. See docs/notes/ui-native.md, "Overview of the surface canvases".

#include <windows.h>

#include <cstdint>
#include <string_view>

class CUiSurface;

namespace render::text {
class TextFont;
} // namespace render::text

namespace ui_native {

// The surface was made (its holder new and black): a canvas for it when `family` goes native.
void Adopt(CUiSurface* surface, const char* family);
// The surface is released or made again: it is no longer native.
void Forget(const CUiSurface* surface);
// The surface's holder has a canvas the surface functions draw on.
bool IsNative(const CUiSurface* surface);

// Before the CPU reads the surface's memory (PixelAt, Rgb565Copy, SwapContents), or before GDI draws into it
// when that drawing is then copied onto the canvas (Text's fallback): the canvas read back into it when
// it is behind.
void Sync(const CUiSurface* surface);
// SwapContents without the memory: a canvas newer than the memory (behind) is put aside under the surface's
// address before the holder goes, and back onto the new holder (TakeBack). Each returns whether it did.
bool PutAside(const CUiSurface* surface);
bool TakeBack(const CUiSurface* surface);

// The surface functions' drawing, in surface pixels (after the game's scaling), recorded on the canvas.
void Fill(const CUiSurface* surface, const RECT& rect, COLORREF colour);
void FrameRect(const CUiSurface* surface, const RECT& rect, COLORREF colour);
// MoveTo (x0, y0), LineTo (x1, y1) with a solid pen of `width` (GDI's pixels for the game's 1 and 2 pixel pens,
// horizontal or vertical: render_ui.h's Line); the console's caret.
void Line(const CUiSurface* surface, int x0, int y0, int x1, int y1, int width, COLORREF colour);
// Text through the glyph atlas (ticket P2; docs/notes/ui-native.md, "P2 done"). A native surface's text is
// laid out by render_text.h's DrawText rules in TextFontOf(font) and drawn on the canvas: `text` (UTF-8) in
// `rect` (surface pixels, the game's scaling done) by DrawText's `format`, first in the shadow colour one
// pixel lower and to the right when `shadow`, then in `colour`. The result says what the caller (DrawTextIn)
// still does:
//   Gdi        nothing was drawn (the surface is not native, the font has no stand-in or the format has bits
//              the layout does not follow): GDI draws it into the memory as before, then TextDrawn. A
//              native surface's memory was brought up to date first (Sync).
//   AlsoGdi    drawn on the canvas; GDI draws it into the memory too, and DrawText's value is GDI's
//              (uiboth: the comparison reads the memory).
//   Canvas     drawn on the canvas only; `*height` is DrawText's value from the layout (the memory lacks the
//              text until Sync reads the canvas back).
enum class TextPath { Gdi, AlsoGdi, Canvas };
TextPath Text(const CUiSurface* surface, HFONT font, std::string_view text, const RECT& rect, UINT format,
              COLORREF colour, bool shadow, int* height);
// The font native text is drawn in for the GDI font `font` (ui_native_fonts.cpp): the glyphs' shapes from our
// own font file standing in for its face (render_text.h, FontFileForFace), placed by the GDI font's metrics
// (ascent, descent, advances, kerning), so lines break, align and measure as GDI's DrawText and the game's
// extent have them. Smoothed unless the font's quality or the system's setting says not. Made on first use and
// kept for the process; null without the font file.
render::text::TextFont* TextFontOf(HFONT font);
// GDI drew text clipped to `rect` (or anywhere on the surface, `clipped` false) into the memory of a native
// surface Text could not draw on (TextPath::Gdi): that part of the memory is copied onto the canvas.
void TextDrawn(const CUiSurface* surface, const RECT& rect, bool clipped);
// The screen's pixels on the GPU only (the canvas becomes the surface's only copy; one is made when the
// surface has none, with the memory's pixels on it except for the copy, which covers it whole): the copy of
// `source` of the frame drawn so far, the greying, the 5:6:5 picture stretched to `width` x `height` and
// read back into `out` (false when it could not be read), and the pixel (x, y) in the memory's format
// (PixelOnGpu: false unless the canvas is newer than the memory).
void Capture(const CUiSurface* surface, const RECT& source);
void Greyscale(const CUiSurface* surface);
bool ReadRgb565(const CUiSurface* surface, int width, int height, std::uint16_t* out);
bool PixelOnGpu(const CUiSurface* surface, int x, int y, std::uint32_t* pixel);
// SwapContents put the memory back (a new holder, the kept bytes copied in): the canvas made again from it.
void Restored(CUiSurface* surface);

// The verify build's per-surface comparison (tests/verify/harness_ui.h): a native surface is about to go
// (ReleaseContents), e.g. the status surface, drawn inside ShowStatusText's own frame and gone before the
// main loop's next one.
extern void (*g_goneObserver)(const CUiSurface* surface);

} // namespace ui_native
