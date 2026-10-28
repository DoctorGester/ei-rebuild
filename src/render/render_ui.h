#pragma once

// The interface's 2D canvases: a window surface's pixels kept in a GPU render target. Single-threaded. See
// docs/notes/ui-native.md, section 3 and "Source file overviews".

#include "render.h"

#include <cstdint>
#include <string>
#include <vector>

namespace render::text {
class TextFont;
struct TextLayout;
} // namespace render::text

namespace render::ui {

// A surface's pixel format (SurfaceMemory's): 16 or 32 bits, the colour masks.
struct PixelFormat {
    int bitsPerPixel = 32;
    std::uint32_t redMask = 0xFF0000, greenMask = 0xFF00, blueMask = 0xFF;
};

// `colour` as a surface of `format` keeps it (each channel's top bits) widened back to 8 bits a
// channel, as 0x00RRGGBB: what the canvas holds and the blit shows.
std::uint32_t Quantise(std::uint32_t colour, const PixelFormat& format);
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
// The canvas becomes the surface's only copy of its pixels (the screen copies and what is made from them):
// the blit draws it whatever the switch says, and the comparison harness leaves it out (the memory is
// not drawn). It stays so until the canvas is dropped.
void CanvasOnly(const void* holder);
// The canvas (and its queued commands) moved from the key `from` to `to`: SwapContents puts a canvas-only
// surface's canvas aside while the surface has no holder and back onto its new one. A blit of it the frame
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
// ReadPixels into the holder's SurfaceMemory (of the canvas's size) in its format, marked written: for
// the code that reads surface pixels (PixelAt, Rgb565Copy).
bool ReadBack(const void* holder);

// Draws the canvas onto the frame at this point of the frame's draws, as SurfaceBlit draws a surface:
// `source` (null: all of it) onto `destination`, stretched when the sizes differ, and with `keyed`
// leaving out pixels equal to `key` (a pixel of the canvas's format).
void DrawCanvas(const void* holder, const Rect& destination, const Rect* source, bool keyed, std::uint32_t key);

// `rgb` (0x00RRGGBB rows from the top, `pitch` pixels apart) written over `destination` of the canvas
// (clipped to it), quantised to the format: a picture, or the surface's memory as drawing the canvas
// cannot record left it (GDI, code not ported).
void Image(const void* holder, const Rect& destination, const std::uint32_t* rgb, int pitch);

// ---- The switch between the two paths ----------------------------------------------------------

// EI_RENDER's interface words (render::Configure): no word, the native surfaces draw as today (GDI into
// their memory, blitted from it); "uiboth": GDI and the canvas both drawn, the blit showing the path
// SetPath chose (the verify build compares the two per surface: tests/verify/harness_ui.h); "uinative":
// the canvas only (GDI kept for the metrics). "uiboth=<a>+<b>" / "uinative=<a>+<b>" limit it to the
// surface families named (Listed).
enum class Mode { Cpu, Both, Native };
enum class Path { Cpu, Native };
// render.cpp's ParseSpec: the defaults before a spec's words, then each word (true when it was one of these).
void ResetOptions();
bool ParseOption(const std::string& word);
Mode CurrentMode();
// Whether surfaces of the family `name` go native: Both or Native, and no list or `name` in it.
bool Listed(const char* name);
// Under Both, which of the two the blits show (default Cpu: the frame as without the canvases).
void SetPath(Path path);
// Whether the blit of `holder` draws its canvas (DrawCanvas) instead of its memory (SurfaceBlit): it has a
// canvas, and the canvas is the only copy (CanvasOnly) or the mode is Native, or Both with the path Native.
bool DrawsCanvas(const void* holder);

// ---- What the comparison harness reads (verify build) -----------------------------------------------------

struct CanvasInfo {
    const void* holder = nullptr;
    int width = 0, height = 0;
    PixelFormat format;
    bool only = false;        // CanvasOnly: the memory is not drawn
    unsigned version = 0;     // changes whenever a command for the canvas is queued (or it is made)
    std::vector<Rect> text;   // where glyph runs drew since the last fill of the whole canvas: their clip
                              // rectangles (unclipped, their bounds), at most 64 (then their union)
};
void Canvases(std::vector<CanvasInfo>* out);
// The destinations of the canvas blits of the frame drawn last (between frames: the last presented one).
void DrawnCanvasRects(std::vector<Rect>* out);

} // namespace render::ui
