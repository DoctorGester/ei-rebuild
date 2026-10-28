// The texture manager's new texture and the blank texture it gets, which the game draws into on the GPU
// (render::BlankTexture): the 1:5:5:5 ones (kind 0x20: the item pictures and the image atlases CopyImage fills,
// ui_item_view.cpp) and the save / load screen's pictures (ui_saveload.cpp) through render::DrawIntoTexture, the global
// map's weather through render::DrawClouds (ui_global_map.cpp). See docs/notes/renderer.md (section 7).
#include "texture.h"

#include "surface.h"

#include "render/render.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint32_t kMmpMagic = 0x504D4D;   // "MMP"
constexpr std::uint32_t kAtlasFormat = 0x5551;   // the item pictures and image atlases

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


void CTexture::TakeBlankHolder(CTextureDevice* device, int width, int height, bool mipmapped) {
    const PixelFormat format = TextureFormat(format_);
    // Without a format (another kind's texture) the error for a surface that cannot be made ends the game.
    Surface* holder = format.bitsPerPixel ? NewDecodedTextureHolder(device, width, height, format.bitsPerPixel) : nullptr;
    if (!holder) ThrowSurfaceNotMade(0, 0);
    const render::BlankAlpha alpha = !format.alphaMask           ? render::BlankAlpha::None
                                     : format.alphaMask == 0x8000 ? render::BlankAlpha::OneBit
                                                                  : render::BlankAlpha::Full;
    render::BlankTexture(holder, width, height, alpha, mipmapped);
    Surface* previous = surface_;
    surface_ = holder;
    ++holder->refs_;
    if (previous) previous->Release();
}

int CTexture::CreateBlank(CTextureDevice* device, const MmpHeader* header) {
    // A square of header->width (the height is not read). The atlases keep one level, as the original's every blank
    // texture (a level below would mix neighbouring cells); the others get their levels made after each draw.
    const auto size = static_cast<int>(header->width);
    TakeBlankHolder(device, size, size, format_ != kAtlasFormat);
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
