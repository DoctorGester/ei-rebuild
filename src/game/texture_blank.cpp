// Textures the game fills on the CPU: the texture manager's new texture (00691D80: the save / load
// screen's pictures, the global map's weather, the item pictures and the other interface textures
// made through 005FF0B0 / 005EC020) and the blank surface it gets (0068F6C0, also used by the
// shadow textures 00692010 / 00692140).
//
// The blank surface is no DirectDraw texture (ticket R8; R11b removed the DirectDraw one): its holder
// has only the new renderer's memory (NewMemoryTextureHolder, dx_surface.cpp), which the writers
// fill through DirectPixels (ui_item_view.cpp, ui_global_map.cpp, ui_saveload.cpp,
// texture_shadow.cpp) and the new renderer reads. The binders' SetTexture gets the holder's null
// surface (004EB300 / 004EBAF0 / 00501AF0). See docs/notes/renderer.md (section 7).
#include "texture.h"

#include "dx_surface.h"
#include "game_strings.h"
#include "originals_engine.h"
#include "originals_render.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint32_t kMmpMagic = 0x504D4D;   // "MMP"

// The formats of 00691D80's kinds: the device's format id and the channels it writes into the
// header (mask, shift, bits; the header's other fields stay zero).
struct Channel {
    std::uint32_t mask, shift, bits;
};
struct BlankFormat {
    int kind;
    int id;
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

// --- 0068F6C0 ----------------------------------------------------------------------------------------

int CTexture::CreateBlank(CTextureDevice* device, const MmpHeader* header) {
    if (block_) gamecrt::operator_delete(reinterpret_cast<void**>(block_)[-1]);
    block_ = nullptr;
    // A square of header->width (the height is not read). The holder: only the new renderer's memory,
    // one level (the original: 004EFA60's DirectDraw texture with header->mipCount levels and caps by
    // the kind flags, which throws, with the holder's sizes zero, when it cannot be made).
    const std::uint32_t size = header->width;
    CDXSurface* surface = NewMemoryTextureHolder(device, static_cast<int>(size), static_cast<int>(size), format_.words);
    if (!surface) ThrowSurfaceNotMade(device, 0, 0);
    CDXSurface* previous = surface_;
    surface_ = surface;
    if (surface) ++surface->refCount;
    if (previous) previous->Release();

    // The byte size is meant to be bits * width * height / 8, but the original takes the bits from
    // the channel split's first field, which 004EDAA0 never writes: it is always 0.
    ChannelLayout layout;
    layout.Split(&format_);
    uploadResult_ = static_cast<int>((layout.bitCount * size * size) >> 3);
    return 1;
}
EI_REWRITTEN(0x0068F6C0, CTexture::CreateBlank);

// --- 00691D80 ----------------------------------------------------------------------------------------

CTexture* CTextureManager::NewTexture(int width, int height, int kind) {
    auto* texture = static_cast<CTexture*>(CreateObject(kind, -1));
    MmpHeader header;
    std::memset(&header, 0, sizeof(header));
    texture->width_ = static_cast<std::uint32_t>(width);
    texture->height_ = static_cast<std::uint32_t>(height);
    if (const BlankFormat* f = FormatOfKind(kind)) {
        PixelFormat format;
        // A missing format: the report's result is returned as the texture (the new texture stays
        // registered with the manager).
        if (!device_->FindFormat(f->id, &format))
            return reinterpret_cast<CTexture*>(
                static_cast<std::uintptr_t>(game::FatalError(game::CantFindTextureFormatText(), kind)));
        texture->format_ = format;
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
        header.format = static_cast<std::uint32_t>(f->id);
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
EI_REWRITTEN(0x00691D80, CTextureManager::NewTexture);
