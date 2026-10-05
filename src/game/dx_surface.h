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

// The same description of the game-owned memory of `holder` in any mode (false without such
// memory): for code that writes a surface through a Lock and keeps the memory beside it up to date.
bool ClientPixels(const void* holder, std::uint32_t* desc);

// "cpuwindows": game-owned memory (a zeroed DIB section of `width` x `height` pixels in
// `pixelFormat`, a DDPIXELFORMAT of 16 or 32-bit RGB) for the holder of a texture the game fills on
// the CPU (the save / load screen's pictures, ui_saveload.cpp). The DirectDraw texture is left as
// it is (a managed texture cannot take client memory): outside the switch-over the game code
// writes both, in the switch-over only the memory (DirectPixels). The new renderer reads the memory
// (render::ClientSurface) and 004EFD20 frees it. True when the holder has such memory afterwards.
bool AttachTextureMemory(void* holder, int width, int height, const std::uint32_t* pixelFormat);
