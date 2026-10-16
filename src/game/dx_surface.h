#pragma once

// The surface holders' pixels in the new renderer's memory (dx_surface.cpp; render::SurfaceMemory).
// No holder has a DirectDraw surface (CDXSurface::surface null): the holders of 004EF7B0 / 004F0060
// and the texture manager's CPU-written textures (00691D80) have only that memory, the .mmp
// textures' holders not even that (the new renderer decodes the file).

#include <windows.h>

#include <cstdint>

struct CDXSurface;
class CTextureDevice;

// The DIB section holding the pixels of `holder` (a CDXSurface), or null when its surface keeps its
// own memory.
HBITMAP ClientBitmap(const void* holder);

// Whether `dc` is a memory DC on a holder's DIB section that 005FDC00 gave out and 005FDC60 has not
// taken back yet (ui_surface_dc.cpp; for the verify build's interface census).
bool IsSurfaceMemoryDc(HDC dc);

// The pixel code reads and writes the memory of `holder` directly (no Lock / Unlock): fills `desc` (a
// DDSURFACEDESC2 as dwords, as a Lock would: size, width, height, pitch, memory, pixel format)
// after flushing GDI; false for a null holder or one without such memory.
bool DirectPixels(const void* holder, std::uint32_t* desc);

// The same description of the memory of `holder` (false without such memory).
bool ClientPixels(const void* holder, std::uint32_t* desc);

// Memory (a zeroed DIB section of `width` x `height` pixels in `pixelFormat`, a DDPIXELFORMAT of 16
// or 32-bit RGB) for a texture's holder that has none. The new renderer reads the memory and
// 004EFD20 frees it. True when the holder has such memory afterwards (also when it had some
// already: the textures of NewMemoryTextureHolder, which the save / load screen's pictures are).
bool AttachTextureMemory(void* holder, int width, int height, const std::uint32_t* pixelFormat);

// A texture holder as 004EFA60 makes one (vtable, device, width, height, bits; the holder count one
// higher), allocated on the game's heap, with no DirectDraw surface, only the new renderer's memory
// of `width` x `height` pixels in `pixelFormat` (a DDPIXELFORMAT of 16 or 32-bit RGB). For the
// textures the game writes on the CPU (the texture manager's 00691D80: the save / load pictures, the
// global map's weather, the item pictures; the shadow textures; .bmp textures); the writers use
// DirectPixels. Null when the memory cannot be made (the callers then throw ThrowSurfaceNotMade).
CDXSurface* NewMemoryTextureHolder(CTextureDevice* device, int width, int height, const std::uint32_t* pixelFormat);

// A texture holder as 004EFA60 makes one (as above: vtable, device, width, height, the bits of
// `pixelFormat`, a DDPIXELFORMAT; the holder count one higher) with no DirectDraw surface and no
// memory: for a texture the new renderer decodes from its file (the texture manager's .mmp
// textures, 00690430: render::TextureFromMmp keyed by the holder). Nothing of the game writes or
// locks such a texture. Null only when its allocation fails.
CDXSurface* NewDecodedTextureHolder(CTextureDevice* device, int width, int height, const std::uint32_t* pixelFormat);

// What the original does when DirectDraw cannot make a surface (004EFA60 / 004EF7B0): the device's
// result set to DDERR_UNSUPPORTED (as the refused CreateSurface answered) and the game's error
// "Can't create DDSurface %d %d ..." thrown with `width` and `height`; that ends the game.
[[noreturn]] void ThrowSurfaceNotMade(CTextureDevice* device, int width, int height);
