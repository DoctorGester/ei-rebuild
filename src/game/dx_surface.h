#pragma once

// The off-screen surface holder's game-owned memory ("cpuwindows", dx_surface.cpp).

#include <windows.h>

#include <cstdint>

// The DIB section holding the pixels of `holder` (a CDXSurface), or null when its surface keeps its
// own memory.
HBITMAP ClientBitmap(const void* holder);

// The switch-over ("present") with game-owned memory: the pixel code reads and writes the memory of
// `holder` directly, without DirectDraw (nothing of the old path draws from these surfaces then).
// Fills `desc` (a DDSURFACEDESC2 as dwords, as a Lock would: size, width, height, pitch, memory,
// pixel format) after flushing GDI; false otherwise (lock the surface as before).
bool DirectPixels(const void* holder, std::uint32_t* desc);
