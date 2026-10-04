#pragma once

// The off-screen surface holder's game-owned memory ("cpuwindows", dx_surface.cpp).

#include <windows.h>

// The DIB section holding the pixels of `holder` (a CDXSurface), or null when its surface keeps its
// own memory.
HBITMAP ClientBitmap(const void* holder);
