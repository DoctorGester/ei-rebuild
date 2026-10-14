// Textures the game fills on the CPU: the texture manager's new texture (00691D80: the save / load
// screen's pictures, the global map's weather, the item pictures and the other interface textures
// made through 005FF0B0 / 005EC020) and the blank surface it gets (0068F6C0, also used by the
// shadow textures 00692010 / 00692140).
//
// In the switch-over ("present" with "cpuwindows", ticket R8) the textures of 00691D80 have no
// DirectDraw texture: their holder has only the new renderer's memory (NewMemoryTextureHolder,
// dx_surface.cpp), which the writers fill through DirectPixels (ui_item_view.cpp, ui_global_map.cpp,
// ui_saveload.cpp) and the new renderer reads. The old path's SetTexture then binds no texture for
// them (004EB300 / 004EBAF0 / 00501AF0 pass the holder's null surface), and the texture manager
// touches their surface nowhere else (SetPriority in the cache, texture_cache.cpp, is left out for a
// holder without a surface). The shadow textures (00692010 / 00692140, texture_shadow.cpp) are
// memory-only there too since R8's second part. See docs/notes/renderer.md (sections 15, 16).
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

// DDSURFACEDESC2 (0x7C bytes) as dwords.
enum DescIndex { kSize = 0, kFlags = 1, kHeight = 2, kWidth = 3, kMipCount = 6, kPixelFormat = 18, kCaps = 26,
                 kCaps2 = 27, kDescWords = 31 };
constexpr std::uint32_t kDescFlags = 0x1007;              // CAPS | HEIGHT | WIDTH | PIXELFORMAT
constexpr std::uint32_t kDescFlagsMipMaps = 0x21007;      // ... | MIPMAPCOUNT
constexpr std::uint32_t kCapsTexture = 0x1000;            // DDSCAPS_TEXTURE
constexpr std::uint32_t kCapsMipMaps = 0x401008;          // TEXTURE | MIPMAP | COMPLEX
constexpr std::uint32_t kCapsSystemMemory = 0x800;
constexpr std::uint32_t kCaps2TextureManage = 0x10;
constexpr std::uint32_t kCaps2Opaque = 0x80, kCaps2HintStatic = 0x8, kCaps2HintDynamic = 0x4;

// CEngineObject::flags_ of a texture: its kind as the manager's CreateObject (00691AA0) sets it
// (0x40 the default, 0x80 kind 0x3E, 0x100 kind 0x3F, 0x200 kind 0x40: the shadow textures) and
// 0x400, the shadow's render surface in system memory (00692140).
constexpr std::uint32_t kKindDefault = 0x40, kKindStatic = 0x80, kKindDynamic = 0x700, kSystemMemory = 0x400;

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

int CTexture::CreateBlank(CTextureDevice* device, const MmpHeader* header) { return MakeBlank(device, header, false); }

int CTexture::MakeBlank(CTextureDevice* device, const MmpHeader* header, bool memoryOnly) {
    if (block_) gamecrt::operator_delete(reinterpret_cast<void**>(block_)[-1]);
    std::uint32_t desc[kDescWords] = {};
    desc[kSize] = sizeof(desc);
    desc[kFlags] = kDescFlags;
    desc[kCaps] = kCapsTexture;
    std::memcpy(&desc[kPixelFormat], &format_, sizeof(PixelFormat));
    block_ = nullptr;
    if (header->mipCount > 1) {
        desc[kFlags] = kDescFlagsMipMaps;
        desc[kCaps] = kCapsMipMaps;
        desc[kMipCount] = header->mipCount;
    }
    std::uint32_t flags = flags_;
    if (flags & kSystemMemory)
        desc[kCaps] |= kCapsSystemMemory;
    else
        desc[kCaps2] = kCaps2TextureManage;
    desc[kWidth] = header->width;
    desc[kHeight] = header->width;
    if (flags & kKindDefault)
        desc[kCaps2] |= kCaps2Opaque;
    else if (flags & kKindStatic)
        desc[kCaps2] |= kCaps2HintStatic;
    else if (flags & kKindDynamic)
        desc[kCaps2] |= kCaps2HintDynamic;

    // The holder: in the switch-over (memoryOnly) only the new renderer's memory, else 004EFA60's
    // DirectDraw texture (which throws when it cannot be made).
    CDXSurface* surface = nullptr;
    if (memoryOnly)
        surface = NewMemoryTextureHolder(device, static_cast<int>(desc[kWidth]), static_cast<int>(desc[kHeight]),
                                         format_.words);
    if (!surface) {
        void* memory = gamecrt::operator_new(sizeof(CDXSurface));
        if (memory) surface = static_cast<CDXSurface*>(DXSurfaceFromDescOriginal(memory, nullptr, device, desc));
    }
    CDXSurface* previous = surface_;
    surface_ = surface;
    if (surface) ++surface->refCount;
    if (previous) previous->Release();

    // The byte size is meant to be bits * width * height / 8, but the original takes the bits from
    // the channel split's first field, which 004EDAA0 never writes: it is always 0.
    ChannelLayout layout;
    layout.Split(&format_);
    uploadResult_ = static_cast<int>((layout.unknown_00 * desc[kHeight] * desc[kWidth]) >> 3);
    return 1;
}
EI_REPLACE_METHOD(0x0068F6C0, CTexture::CreateBlank);

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
    if (!texture->MakeBlank(device_, &header, true)) {
        texture->Release();
        return nullptr;
    }
    return texture;
}
EI_REPLACE_METHOD(0x00691D80, CTextureManager::NewTexture);
