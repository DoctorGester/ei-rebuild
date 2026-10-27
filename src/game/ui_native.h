#pragma once

// The interface surfaces drawn on GPU canvases (docs/notes/ui-native.md, "P1"). A surface of a family EI_RENDER's "uiboth" /
// "uinative" lists gets a canvas when it is made (Adopt); the surface functions record their drawing on it beside GDI and
// the CPU code, and the memory stays the source of truth. Without a "ui" word in EI_RENDER nothing is adopted and every
// function here does nothing. See docs/notes/ui-native.md, "Overview of the surface canvases".

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
// The copy of `source` of the screen and the greying, on the canvas. Each returns whether
// the memory gets it too (the CPU code runs): false under uinative for the copy (the memory falls behind)
// and for greying a memory that is behind.
bool Capture(const CUiSurface* surface, const RECT& source);
bool Greyscale(const CUiSurface* surface);
// SwapContents put the memory back (a new holder, the kept bytes copied in): the canvas made again from it.
void Restored(CUiSurface* surface);

// The verify build's per-surface comparison (tests/verify/harness_ui.h): a native surface is about to go
// (ReleaseContents), e.g. the status surface, drawn inside ShowStatusText's own frame and gone before the
// main loop's next one.
extern void (*g_goneObserver)(const CUiSurface* surface);

} // namespace ui_native
