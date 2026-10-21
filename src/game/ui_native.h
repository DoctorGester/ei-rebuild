#pragma once

// The interface surfaces drawn on GPU canvases (ticket P1; docs/notes/ui-native.md, "P1"). A surface of a
// family EI_RENDER's "uiboth" / "uinative" lists (render::ui::Listed: backdrop, fps, status so far) gets a
// canvas for its holder when it is made (Adopt), and the surface functions record their drawing on it
// beside GDI and the CPU code, which keep drawing the holder's memory: the memory stays the source of
// truth (a lost device brings canvases back black). The blit 005FDA80 draws the canvas when
// render::ui::DrawsCanvas says so.
//
// Under "uinative" the screen copy (CaptureScreen) goes to the canvas only (no read-back of the frame), and
// greying a surface whose memory is behind its canvas too; that memory is "behind" until Sync reads the
// canvas back, which the code that reads or draws the memory on the CPU calls first (GetDC, PixelAt,
// Rgb565Copy, SwapContents). Without a "ui" word in EI_RENDER nothing is adopted and every function here
// does nothing.

#include <windows.h>

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

// The surface functions' drawing, in surface pixels (after the game's scaling), recorded on the canvas.
void Fill(const CUiSurface* surface, const RECT& rect, COLORREF colour);
void FrameRect(const CUiSurface* surface, const RECT& rect, COLORREF colour);
// GDI drew text clipped to `rect` (or anywhere on the surface, `clipped` false) into the memory: until
// text is recorded (P2) that part of the memory is copied onto the canvas.
void TextDrawn(const CUiSurface* surface, const RECT& rect, bool clipped);
// 005FD3D0's copy of `source` of the screen and 005FE440's greying, on the canvas. Each returns whether
// the memory gets it too (the CPU code runs): false under uinative for the copy (the memory falls behind)
// and for greying a memory that is behind.
bool Capture(const CUiSurface* surface, const RECT& source);
bool Greyscale(const CUiSurface* surface);
// SwapContents put the memory back (a new holder, the kept bytes copied in): the canvas made again from it.
void Restored(CUiSurface* surface);

} // namespace ui_native
