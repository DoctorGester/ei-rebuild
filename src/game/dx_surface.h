#pragma once

// The off-screen surface holder's pixels in the new renderer's memory ("cpuwindows",
// dx_surface.cpp; render::SurfaceMemory). In the switch-over ("present") the holders of 004EF7B0 /
// 004F0060 and the texture manager's CPU-written textures (00691D80) have no DirectDraw surface at
// all (CDXSurface::surface null), only that memory.

#include <windows.h>

#include <cstdint>

struct CDXSurface;
class CTextureDevice;

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
// frees it. True when the holder has such memory afterwards (also when it had some already: the
// textures of NewMemoryTextureHolder).
bool AttachTextureMemory(void* holder, int width, int height, const std::uint32_t* pixelFormat);

// In the switch-over with "cpuwindows": a texture holder as 004EFA60 makes one (vtable, device,
// width, height, bits; the holder count one higher), allocated on the game's heap, with no
// DirectDraw surface, only the new renderer's memory of `width` x `height` pixels in `pixelFormat`
// (a DDPIXELFORMAT of 16 or 32-bit RGB). For the textures the game writes on the CPU (the texture
// manager's 00691D80: the save / load pictures, the global map's weather, the item pictures); the
// writers use DirectPixels, the old path's SetTexture binds no texture for it. Null otherwise
// (outside the switch-over, or no memory: then the caller makes the DirectDraw texture).
CDXSurface* NewMemoryTextureHolder(CTextureDevice* device, int width, int height, const std::uint32_t* pixelFormat);

// In the switch-over with "cpuwindows": a texture holder as 004EFA60 makes one (as above: vtable,
// device, width, height, the bits of `pixelFormat`, a DDPIXELFORMAT; the holder count one higher)
// with no DirectDraw surface and no memory: for a texture the new renderer decodes from its file
// (the texture manager's .mmp textures, 00690430: render::TextureFromMmp keyed by the holder).
// Nothing of the game writes or locks such a texture. Null outside the switch-over.
CDXSurface* NewDecodedTextureHolder(CTextureDevice* device, int width, int height, const std::uint32_t* pixelFormat);
