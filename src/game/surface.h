#pragma once

// The game's surfaces and textures without DirectDraw (surface.cpp): the surface holder (Surface), the pixel format
// of its memory and the pixel code's access to it. A holder's pixels live in memory the new renderer owns
// (render::SurfaceMemory, a DIB section), which game code reads and writes directly (DirectPixels); a texture the
// renderer decodes from its .mmp file has no memory. See docs/notes/renderer.md, sections 6 and 7.

#include "renderer_base.h"

#include <windows.h>

#include <cstdint>

class CTextureDevice;
struct MmpHeader;

// The pixel format of a holder's memory, as the renderer's memory takes it (render::NewSurfaceMemory): 16 or 32 bits a
// pixel with the channels at their masks (`alphaMask` 0: no alpha). All zero: no format (a compressed texture's).
struct PixelFormat {
    int bitsPerPixel = 0;
    std::uint32_t redMask = 0, greenMask = 0, blueMask = 0, alphaMask = 0;
};

// The screen's pixel format for a colour depth: 16 bits 5:6:5, otherwise X8R8G8B8.
PixelFormat ScreenFormat(int bitsPerPixel);

// A pixel format's channels as mask, bit count and shift.
struct ChannelLayout {
    std::uint32_t alphaMask = 0, alphaBits = 0, alphaShift = 0;
    std::uint32_t redMask = 0, redBits = 0, redShift = 0;
    std::uint32_t greenMask = 0, greenBits = 0, greenShift = 0;
    std::uint32_t blueMask = 0, blueBits = 0, blueShift = 0;
    // False for no format and at the first channel wider than 8 bits (the channels before it stay filled).
    bool Split(const PixelFormat& format);
    // Each channel's top bits at its shift.
    std::uint32_t ToNative(std::uint32_t argb) const;
    // Each channel's bits moved to the top of its byte (the low bits stay 0).
    std::uint32_t FromNative(std::uint32_t pixel) const;
};

// A surface or texture holder, counted: the interface windows', the software cursor's and the movies' surfaces, the
// textures. Its pixels are the renderer's memory for it, or the texture the renderer decoded for it.
struct Surface final : CRendererBase {
    // No pixels yet; the maker sets the size.
    explicit Surface(CTextureDevice* device);
    // The renderer's memory of `width` x `height` pixels in the screen's format. Throws the game's error object when
    // it cannot be made.
    Surface(CTextureDevice* device, int width, int height);
    // An 8888 .mmp picture (the software cursor's images): memory of its size in the screen's format with the picture
    // converted into it, the renderer keeping the picture's own pixels. Throws the game's error object when the picture
    // cannot be loaded.
    Surface(CTextureDevice* device, const MmpHeader* picture);
    ~Surface() override;

    CTextureDevice* device;
    int width = 0;
    int height = 0;
    int bitsPerPixel = 0;
};

// The DIB section holding the pixels of `holder` (a Surface), or null when it has no memory.
HBITMAP ClientBitmap(const void* holder);

// Whether `dc` is a memory DC on a holder's DIB section that was given out and not taken back yet
// (ui_surface_dc.cpp; for the verify build's interface census).
bool IsSurfaceMemoryDc(HDC dc);

// A holder's memory as the pixel code reads and writes it.
struct SurfacePixels {
    int width = 0;
    int height = 0;
    int pitch = 0;                   // bytes from one row to the next
    std::uint8_t* bits = nullptr;    // the top row
    PixelFormat format;
};

// The pixel code reads and writes the memory of `holder` directly: describes it into `pixels` after flushing GDI;
// false, with `pixels` empty, for a null holder or one without memory. Unless `reads` (the caller only reads it), the
// whole memory is marked written for the new renderer (render::SurfaceWritten): the caller writes it before the
// surface's next blit.
bool DirectPixels(const void* holder, SurfacePixels* pixels, bool reads = false);

// A texture holder (the holder count one higher) with the renderer's memory of `width` x `height` pixels in `format`
// (16 or 32-bit RGB). For the textures the game writes on the CPU (the texture manager's: the save / load pictures, the
// global map's weather, the item pictures; .bmp textures); the writers use DirectPixels. Null when the memory cannot be
// made (the callers then throw ThrowSurfaceNotMade).
Surface* NewMemoryTextureHolder(CTextureDevice* device, int width, int height, const PixelFormat& format);

// A texture holder (the holder count one higher) with no memory, for a texture the renderer decodes from its file (the
// texture manager's .mmp textures: render::TextureFromMmp keyed by the holder) or draws on the GPU (the composed
// textures: render::ComposeTexture). The game's code writes no pixels of it. Null only when its allocation fails.
Surface* NewDecodedTextureHolder(CTextureDevice* device, int width, int height, int bitsPerPixel);

// What happens when a holder's memory cannot be made: the game's error "Can't create DDSurface %d %d ..." thrown with
// `width` and `height`; that ends the game.
[[noreturn]] void ThrowSurfaceNotMade(int width, int height);
