#pragma once

// The game's surfaces and textures without DirectDraw (surface.cpp): the surface holder (Surface) and the pixel format.
// No holder has memory: a software cursor picture's pixels are the renderer's copy of the file's pixels
// (render::PictureFromMmp); a texture's are decoded from its file or drawn on the GPU. See docs/notes/renderer.md, sections 6 and 7.

#include "engine/scene/renderer_base.h"
#include "render/render.h"

#include <cstdint>

class TextureDevice;
struct MmpFile;

// The screen's pixel format: X8R8G8B8.
render::PixelFormat ScreenFormat();

// A pixel format's channels as mask, bit count and shift.
struct ChannelLayout {
    std::uint32_t alphaMask = 0, alphaBits = 0, alphaShift = 0;
    std::uint32_t redMask = 0, redBits = 0, redShift = 0;
    std::uint32_t greenMask = 0, greenBits = 0, greenShift = 0;
    std::uint32_t blueMask = 0, blueBits = 0, blueShift = 0;
    // False for no format and at the first channel wider than 8 bits (the channels before it stay filled).
    bool Split(const render::PixelFormat& format);
    // Each channel's top bits at its shift.
    std::uint32_t ToNative(std::uint32_t argb) const;
    // Each channel's bits moved to the top of its byte (the low bits stay 0).
    std::uint32_t FromNative(std::uint32_t pixel) const;
};

// A surface or texture holder, counted: the software cursor's surfaces, the textures. Its pixels are the picture the
// renderer kept for it or the texture the renderer decoded for it.
struct Surface final : RendererBase {
    // No pixels yet; the maker sets the size.
    explicit Surface(TextureDevice* device);
    // An 8888 .mmp picture (the software cursor's images): its size in the screen's format, the renderer keeping the
    // picture's pixels. Throws the game's error object when the picture cannot be loaded.
    Surface(TextureDevice* device, const MmpFile& picture);
    ~Surface() override;

    TextureDevice* device = nullptr;
    int width = 0;
    int height = 0;
    int bitsPerPixel = 0;
};

// A texture holder (the holder count one higher) with no memory, for a texture the renderer decodes from its file (the
// texture manager's .mmp textures: render::TextureFromMmp keyed by the holder) or draws on the GPU (the composed
// textures: render::ComposeTexture; the ones the game draws into: render::BlankTexture). The game's code writes no
// pixels of it; no texture has memory. Null only when its allocation fails.
Surface* NewDecodedTextureHolder(TextureDevice* device, int width, int height, int bitsPerPixel);

// What happens when a holder's memory cannot be made: the game's error "Can't create DDSurface %d %d ..." thrown with
// `width` and `height`; that ends the game.
[[noreturn]] void ThrowSurfaceNotMade(int width, int height);
