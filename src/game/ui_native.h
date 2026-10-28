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

class CUiSurface;

namespace ui_native {

// The surface was made (its holder new and black): a canvas for it when `family` goes native.
void Adopt(CUiSurface* surface, const char* family);
// The surface is released or made again: it is no longer native.
void Forget(const CUiSurface* surface);
// The surface's holder has a canvas the surface functions draw on.
bool IsNative(const CUiSurface* surface);

// Before the CPU reads or draws the surface's memory: the canvas read back into it when it is behind.
void Sync(const CUiSurface* surface);
// SwapContents without the memory: a canvas newer than the memory (behind) is put aside under the surface's
// address before the holder goes, and back onto the new holder (TakeBack). Each returns whether it did.
bool PutAside(const CUiSurface* surface);
bool TakeBack(const CUiSurface* surface);

// The surface functions' drawing, in surface pixels (after the game's scaling), recorded on the canvas.
void Fill(const CUiSurface* surface, const RECT& rect, COLORREF colour);
void FrameRect(const CUiSurface* surface, const RECT& rect, COLORREF colour);
// GDI drew text clipped to `rect` (or anywhere on the surface, `clipped` false) into the memory: until
// text is recorded (P2) that part of the memory is copied onto the canvas.
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
