// .mmp textures: a texture's holder for the levels the new renderer decodes from the file data (keyed by the holder,
// render::TextureFromMmp; the holder has no memory: NewDecodedTextureHolder, surface.cpp), the mip offsets and the
// file's extra block. The byte size the texture cache counts is what the original's fill returned
// (MmpHeader::FilledBytes). A texture the new renderer does not decode (a format it does not know) keeps its holder
// and shows nothing. See docs/notes/renderer.md (section 7).
#include "texture.h"

#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "render/render.h"
#include "surface.h"

#include "../hook/log.h"
#include "game_data.h"
#include "game_singletons.h"
#include "options.h"
#include "lib/str.h"


namespace {


// Nonzero when mipmaps are used (otherwise every texture gets a single level).

constexpr std::uint32_t kDxtTag = 0x545844;          // "DXT" (the format's low three bytes)

bool g_undecodedLogged = false;

// A holder with no DirectDraw texture for the levels the new renderer decodes from the file (keyed
// by the holder). One it does not decode keeps the holder and shows nothing (logged once). Its
// allocation failing ends the game, like a surface that could not be made.
Surface* DecodedHolder(CTextureDevice* device, const CTexture* texture, const MmpHeader* data, unsigned mip,
                       unsigned levels) {
    Surface* holder = NewDecodedTextureHolder(device, static_cast<int>(texture->width_), static_cast<int>(texture->height_),
                                              TextureFormat(texture->format_).bitsPerPixel);
    if (!holder) ThrowSurfaceNotMade(0, 0);
    if (!render::TextureFromMmp(holder, data, mip, levels) && !g_undecodedLogged) {
        g_undecodedLogged = true;
        ei::Log("cpuwindows: an .mmp texture the new renderer does not decode (format %08X) shows nothing",
                data->format);
    }
    return holder;
}

} // namespace


void CTexture::MarkLoaded(const MmpHeader* header) {
    flags_ &= 0xFFFC3FFF;
    switch (header->alphaKind) {
    case 0: flags_ |= 0x4000; break;
    case 1: flags_ |= 0x8000; break;
    case 4: flags_ |= 0x10000; break;
    case 8: flags_ |= 0x20000; break;
    default: break;
    }
    flags_ |= kTextureLoaded;
}

std::uint32_t MmpHeader::MipOffset(unsigned mip) const {
    if (mip == 0) return extraSize;
    std::uint32_t levelBytes = (bitsPerPixel * height * width) >> 3;
    std::uint32_t scale = 1u << ((2 * mip) & 31);   // 4^mip (SHL masks the count)
    std::uint32_t larger = levelBytes * (scale - 1) * 4 / (scale * 3);
    return larger + extraSize;
}

std::uint32_t MmpHeader::FilledBytes(std::uint32_t heldFormat, unsigned mip, unsigned levels) const {
    std::uint32_t bits = bitsPerPixel;
    if ((format & 0xFFFFFF) == kDxtTag && heldFormat != format) bits = 16;   // decoded into an RGB format
    std::uint32_t w = width >> (mip & 31);
    std::uint32_t h = height >> (mip & 31);
    std::uint32_t total = 0;
    for (unsigned m = mip; m < mipCount && m < mip + levels; ++m) {
        total += (bits * h * w) >> 3;
        w >>= 1;
        h >>= 1;
    }
    return total;
}


int CTexture::CreateFromMmp(CTextureDevice* device, const MmpHeader* header, unsigned mip) {
    if (flags_ & kTextureLoaded) return 1;
    header->MipOffset(mip);   // the result is not used

    std::uint32_t width = header->width >> (mip & 31);
    std::uint32_t height = header->height >> (mip & 31);
    std::uint32_t levels = header->mipCount - mip;
    if (Manager()->limitTo512_) {
        while (width >= 0x200 || height >= 0x200) {
            ++mip;
            width = header->width >> (mip & 31);
            height = header->height >> (mip & 31);
        }
        levels = header->mipCount - mip;
    }
    if (!options::g_options->mipmapping_) levels = 1;
    height_ = height;
    width_ = width;

    // The new renderer decodes the levels from the file and the holder has no DirectDraw texture
    // (a system-memory staging surface filled from the file, loaded into a managed
    // texture with the kind's priority hints, its priority set to 100, is not made).
    Surface* decoded = DecodedHolder(device, this, header, mip, levels);
    Surface* previous = surface_;
    surface_ = decoded;
    ++decoded->refs_;
    if (previous) previous->Release();
    uploadResult_ = static_cast<int>(header->FilledBytes(format_, mip, levels));
    MarkLoaded(header);
    extra_.resize(static_cast<int>(header->extraSize));
    std::memcpy(extra_.data(), header + 1, header->extraSize);
    return 1;
}

int CTextureManager::LoadMmpExtra(const char* name, std::vector<std::uint8_t>* out) {
    if (const ComposedTexture* composed = FindComposed(name)) {
        *out = composed->extra;
        return 1;
    }
    std::string path = str::Format("%s%s", name, ".mmp");   // "%s%s", ".mmp"
    auto* fileHeader = static_cast<MmpHeader*>(GetFile(path, nullptr));
    if (!fileHeader) {
        out->clear();
        return 0;
    }
    out->resize(static_cast<int>(fileHeader->extraSize));
    if (fileHeader->extraSize) std::memcpy(out->data(), fileHeader + 1, fileHeader->extraSize);
    CloseFile(fileHeader);
    return 1;
}
