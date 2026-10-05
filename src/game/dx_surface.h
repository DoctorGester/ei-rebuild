#pragma once

// The off-screen surface holder's pixels in the new renderer's memory ("cpuwindows",
// dx_surface.cpp; render::SurfaceMemory). In the switch-over ("present") the holders of 004EF7B0 /
// 004F0060 have no DirectDraw surface at all (CDXSurface::surface null), only that memory.

#include <windows.h>

#include <cstdint>

// The DIB section holding the pixels of `holder` (a CDXSurface), or null when its surface keeps its
// own memory.
HBITMAP ClientBitmap(const void* holder);

// The pixel code reads and writes the memory of `holder` directly, without DirectDraw: in the
// switch-over ("present") for any holder with such memory (nothing of the old path draws from these
// surfaces then), and always for a holder with no DirectDraw surface. Fills `desc` (a DDSURFACEDESC2
// as dwords, as a Lock would: size, width, height, pitch, memory, pixel format) after flushing GDI;
// false otherwise (lock the surface as before).
bool DirectPixels(const void* holder, std::uint32_t* desc);

// The same description of the memory of `holder` in any mode (false without such memory): for code
// that writes a surface through a Lock and keeps the memory beside it up to date.
bool ClientPixels(const void* holder, std::uint32_t* desc);

// "cpuwindows": memory (a zeroed DIB section of `width` x `height` pixels in `pixelFormat`, a
// DDPIXELFORMAT of 16 or 32-bit RGB) for the holder of a texture the game fills on the CPU (the
// save / load screen's pictures, ui_saveload.cpp). The DirectDraw texture is left as it is (a
// managed texture cannot take client memory): outside the switch-over the game code writes both, in
// the switch-over only the memory (DirectPixels). The new renderer reads the memory and 004EFD20
// frees it. True when the holder has such memory afterwards.
bool AttachTextureMemory(void* holder, int width, int height, const std::uint32_t* pixelFormat);
