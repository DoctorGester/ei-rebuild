// .mmp textures: creating a texture's Direct3D surfaces from the file data, the mip
// offsets and the file's extra block.
// An .mmp texture has no DirectDraw texture: the new
// renderer decodes the levels the game would load from the file data (keyed by the holder,
// render::TextureFromMmp) and the holder has no surface and no memory (NewDecodedTextureHolder,
// dx_surface.cpp); no staging surface, fill, Load or priority. The byte size the texture
// cache counts is what the fill would have returned (MmpHeader::FilledBytes). A texture the new
// renderer does not decode (a format it does not know, or the renderer off after a failure) keeps
// its holder and shows nothing. See docs/notes/renderer.md (section 7).
#include "texture.h"

#include "lib/file_io.h"
#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "render/render.h"
#include "dx_surface.h"

#include "../hook/log.h"
#include "game_data.h"
#include "game_singletons.h"
#include "options.h"
#include "lib/str.h"


namespace {


// Nonzero when mipmaps are used (otherwise every texture gets a single level).
int MipmapsEnabled() { return options::TheOptions()->mipmapping_; }

// The alpha kind from the file (texture flags 0x4000..0x20000), and the texture loaded.
void MarkLoaded(CTexture* texture, const MmpHeader* header) {
    texture->flags_ &= 0xFFFC3FFF;
    switch (header->alphaKind) {
    case 0: texture->flags_ |= 0x4000; break;
    case 1: texture->flags_ |= 0x8000; break;
    case 4: texture->flags_ |= 0x10000; break;
    case 8: texture->flags_ |= 0x20000; break;
    default: break;
    }
    texture->flags_ |= kTextureLoaded;
}

constexpr std::uint32_t kDxtTag = 0x545844;          // "DXT" (the format's low three bytes)
constexpr std::uint32_t kFormat8888 = 0x8888;
constexpr std::uint32_t kPixelFormatFourCC = 0x4;   // DDPIXELFORMAT flags

bool g_undecodedLogged = false;

// A holder with no DirectDraw texture for the levels the new renderer decodes from the file (keyed
// by the holder). One it does not decode keeps the holder and shows nothing (logged once). Its
// allocation failing ends the game, like a surface that could not be made.
CDXSurface* DecodedHolder(CTextureDevice* device, const CTexture* texture, const MmpHeader* data, unsigned mip,
                          unsigned levels) {
    CDXSurface* holder = NewDecodedTextureHolder(device, static_cast<int>(texture->width_),
                                                 static_cast<int>(texture->height_), texture->format_.words);
    if (!holder) ThrowSurfaceNotMade(device, 0, 0);
    if (!render::TextureFromMmp(holder, data, mip, levels) && !g_undecodedLogged) {
        g_undecodedLogged = true;
        ei::Log("cpuwindows: an .mmp texture the new renderer does not decode (format %08X) shows nothing",
                data->format);
    }
    return holder;
}

} // namespace


std::uint32_t MmpHeader::MipOffset(unsigned mip) const {
    if (mip == 0) return extraSize;
    std::uint32_t levelBytes = (bitsPerPixel * height * width) >> 3;
    std::uint32_t scale = 1u << ((2 * mip) & 31);   // 4^mip (SHL masks the count)
    std::uint32_t larger = levelBytes * (scale - 1) * 4 / (scale * 3);
    return larger + extraSize;
}

std::uint32_t MmpHeader::FilledBytes(const PixelFormat& surfaceFormat, unsigned mip, unsigned levels) const {
    std::uint32_t bits = bitsPerPixel;
    if ((format & 0xFFFFFF) == kDxtTag) {
        if (!(surfaceFormat.words[1] & kPixelFormatFourCC) || format != surfaceFormat.words[2]) bits = 16;
    } else if (format == kFormat8888 && surfaceFormat.words[3] != 32) {
        bits = surfaceFormat.words[3];
    }
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
    if (!MipmapsEnabled()) levels = 1;
    height_ = height;
    width_ = width;

    // The new renderer decodes the levels from the file and the holder has no DirectDraw texture
    // (a system-memory staging surface filled from the file, loaded into a managed
    // texture with the kind's priority hints, its priority set to 100, is not made).
    CDXSurface* decoded = DecodedHolder(device, this, header, mip, levels);
    CDXSurface* previous = surface_;
    surface_ = decoded;
    ++decoded->refs_;
    if (previous) previous->Release();
    uploadResult_ = static_cast<int>(header->FilledBytes(format_, mip, levels));
    MarkLoaded(this, header);
    extra_.resize(static_cast<int>(header->extraSize));
    std::memcpy(extra_.data(), header + 1, header->extraSize);
    return 1;
}

int CTextureManager::LoadMmpExtra(const char* name, ByteBuffer* out) {
    MmpHeader header;
    std::memset(&header, 0, sizeof(header));
    std::string path;
    lib::File file;
    path = str::Format("%stextures\\%s.mmp", DataFolderText(), name);
    if (file.Open(path, lib::File::modeRead)) {
        file.Read(&header, sizeof(header));
        int size = static_cast<int>(header.extraSize);
        out->resize(size);
        if (size) file.Read(out->data(), static_cast<unsigned>(size));
        file.Close();
        return 1;
    }
    path = str::Format("%s%s", name, ".mmp");   // "%s%s", ".mmp"
    auto* fileHeader = static_cast<MmpHeader*>(GetFile(path.c_str(), nullptr));
    if (!fileHeader) {
        out->clear();
        return 0;
    }
    out->resize(static_cast<int>(fileHeader->extraSize));
    if (fileHeader->extraSize) std::memcpy(out->data(), fileHeader + 1, fileHeader->extraSize);
    CloseFile(fileHeader);
    return 1;
}
