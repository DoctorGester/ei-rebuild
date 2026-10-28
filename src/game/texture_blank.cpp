// The texture manager's new texture and the blank surface it gets. The 1:5:5:5 ones (kind 0x20: the item pictures and the
// image atlases CopyImage fills, ui_item_view.cpp) are drawn into on the GPU (render::BlankTexture / DrawIntoTexture);
// the others (the save / load screen's pictures, the global map's weather) have only the new renderer's memory
// (NewMemoryTextureHolder, surface.cpp), which their writers fill through DirectPixels (ui_global_map.cpp,
// ui_saveload.cpp) and the new renderer reads. See docs/notes/renderer.md (section 7).
#include "texture.h"

#include "surface.h"

#include "render/render.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint32_t kMmpMagic = 0x504D4D;   // "MMP"
constexpr std::uint32_t kGpuDrawnFormat = 0x5551;

// The formats of the texture kinds: the texture format and the channels it writes into the header (mask, shift,
// bits; the header's other fields stay zero).
struct Channel {
    std::uint32_t mask, shift, bits;
};
struct BlankFormat {
    int kind;
    std::uint32_t id;
    Channel alpha, red, green, blue;
};
constexpr BlankFormat kBlankFormats[] = {
    {0x1E, 0x5650, {0, 0, 0}, {0xF800, 11, 5}, {0x7E0, 5, 6}, {0x1F, 0, 5}},
    {0x20, 0x5551, {0x8000, 15, 1}, {0x7C00, 10, 5}, {0x3E0, 5, 5}, {0x1F, 0, 5}},
    {0x21, 0x4444, {0xF000, 12, 4}, {0xF00, 8, 4}, {0xF0, 4, 4}, {0xF, 0, 4}},
};

const BlankFormat* FormatOfKind(int kind) {
    for (const BlankFormat& f : kBlankFormats)
        if (f.kind == kind) return &f;
    return nullptr;
}

} // namespace


int CTexture::CreateBlank(CTextureDevice* device, const MmpHeader* header) {
    // A square of header->width (the height is not read), one level. Without the memory (another kind's texture has no
    // format) the error for a surface that cannot be made ends the game.
    const std::uint32_t size = header->width;
    const PixelFormat format = TextureFormat(format_);
    Surface* surface;
    if (format_ == kGpuDrawnFormat) {
        // The 1:5:5:5 textures (the item pictures, the image atlases CopyImage fills) are drawn into on the GPU.
        surface = NewDecodedTextureHolder(device, static_cast<int>(size), static_cast<int>(size), format.bitsPerPixel);
        if (surface) render::BlankTexture(surface, static_cast<int>(size), static_cast<int>(size), true);
    } else {
        surface = NewMemoryTextureHolder(device, static_cast<int>(size), static_cast<int>(size), format);
    }
    if (!surface) ThrowSurfaceNotMade(0, 0);
    Surface* previous = surface_;
    surface_ = surface;
    ++surface->refs_;
    if (previous) previous->Release();
    // The original meant bits * width * height / 8 but took the bits from a field its channel split never writes.
    uploadResult_ = 0;
    return 1;
}

CTexture* CTextureManager::NewTexture(int width, int height, int kind) {
    CTexture* texture = NewTextureOfType(kind);
    MmpHeader header;
    std::memset(&header, 0, sizeof(header));
    texture->width_ = static_cast<std::uint32_t>(width);
    texture->height_ = static_cast<std::uint32_t>(height);
    if (const BlankFormat* f = FormatOfKind(kind)) {
        texture->format_ = f->id;
        header.alphaMask = f->alpha.mask;
        header.alphaShift = f->alpha.shift;
        header.alphaKind = f->alpha.bits;
        header.redMask = f->red.mask;
        header.redShift = f->red.shift;
        header.redBits = f->red.bits;
        header.greenMask = f->green.mask;
        header.greenShift = f->green.shift;
        header.greenBits = f->green.bits;
        header.blueMask = f->blue.mask;
        header.blueShift = f->blue.shift;
        header.blueBits = f->blue.bits;
        header.bitsPerPixel = 16;
        header.format = f->id;
    }
    header.magic = kMmpMagic;
    header.width = static_cast<std::uint32_t>(width);
    header.height = static_cast<std::uint32_t>(height);
    header.mipCount = 1;
    if (!texture->CreateBlank(device_, &header)) {
        texture->Release();
        return nullptr;
    }
    return texture;
}
