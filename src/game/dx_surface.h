#pragma once

// The surface holders' pixels in the new renderer's memory (dx_surface.cpp; render::SurfaceMemory).
// No holder has a DirectDraw surface (CDXSurface::surface null): the holders of 004EF7B0 / 004F0060
// and the texture manager's CPU-written textures (00691D80) have only that memory, the .mmp
// textures' holders not even that (the new renderer decodes the file).

#include <windows.h>

#include "texture.h"   // PixelFormat

#include <cstdint>

struct CDXSurface;
class CTextureDevice;

// The DIB section holding the pixels of `holder` (a CDXSurface), or null when its surface keeps its
// own memory.
HBITMAP ClientBitmap(const void* holder);

// Whether `dc` is a memory DC on a holder's DIB section that 005FDC00 gave out and 005FDC60 has not
// taken back yet (ui_surface_dc.cpp; for the verify build's interface census).
bool IsSurfaceMemoryDc(HDC dc);

// A holder's memory as the pixel code reads and writes it (what the original's Lock described).
struct SurfacePixels {
    int width = 0;
    int height = 0;
    int pitch = 0;                   // bytes from one row to the next
    std::uint8_t* bits = nullptr;    // the top row
    int bitsPerPixel = 0;            // 16 or 32
    std::uint32_t redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;

    // The format as the game keeps one (a DDPIXELFORMAT: CTexture::format_, ChannelLayout::Split).
    PixelFormat Format() const;
};

// The pixel code reads and writes the memory of `holder` directly (no Lock / Unlock): describes it
// into `pixels` after flushing GDI; false, with `pixels` empty, for a null holder or one without
// such memory. Unless `reads` (the caller only reads it), the whole memory is marked written for the
// new renderer (render::SurfaceWritten): the caller writes it before the surface's next blit.
bool DirectPixels(const void* holder, SurfacePixels* pixels, bool reads = false);

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
