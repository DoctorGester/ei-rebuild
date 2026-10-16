// .mmp textures: creating a texture's Direct3D surfaces from the file data (00690430), the mip
// offsets (006908E0) and the file's extra block (006928C0).
//
// An .mmp texture has no DirectDraw texture (ticket R8; R11b removed the DirectDraw one): the new
// renderer decodes the levels the game would load from the file data (keyed by the holder,
// render::TextureFromMmp) and the holder has no surface and no memory (NewDecodedTextureHolder,
// dx_surface.cpp); no staging surface, fill (0068FA10), Load or priority. The byte size the texture
// cache counts is what the fill would have returned (MmpHeader::FilledBytes). A texture the new
// renderer does not decode (a format it does not know, or the renderer off after a failure) keeps
// its holder and shows nothing. See docs/notes/renderer.md (section 7).
#include "texture.h"
#include "game_strings.h"

#include "ei/hook.h"

#include "lib/crt.h"
#include "lib/mfc.h"
#include "render/render.h"
#include "dx_surface.h"
#include "originals_render.h"

#include "../hook/log.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

const char* DataPath() { return *reinterpret_cast<const char* const*>(0x007AF460); }

// Nonzero when mipmaps are used (otherwise every texture gets a single level).
int MipmapsEnabled() { return *reinterpret_cast<const int*>(0x0079BCE0); }

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
// allocation failing ends the game as the original's surface that could not be made.
CDXSurface* DecodedHolder(CTextureDevice* device, const CTexture* texture, const void* data, unsigned mip,
                          unsigned levels) {
    CDXSurface* holder = NewDecodedTextureHolder(device, static_cast<int>(texture->width_),
                                                 static_cast<int>(texture->height_), texture->format_.words);
    if (!holder) ThrowSurfaceNotMade(device, 0, 0);
    if (!render::TextureFromMmp(holder, data, mip, levels) && !g_undecodedLogged) {
        g_undecodedLogged = true;
        ei::Log("cpuwindows: an .mmp texture the new renderer does not decode (format %08X) shows nothing",
                static_cast<const MmpHeader*>(data)->format);
    }
    return holder;
}

} // namespace

// --- MmpHeader -----------------------------------------------------------------------------------

std::uint32_t MmpHeader::MipOffset(unsigned mip) const {
    if (mip == 0) return extraSize;
    std::uint32_t levelBytes = (bitsPerPixel * height * width) >> 3;
    std::uint32_t scale = 1u << ((2 * mip) & 31);   // 4^mip (SHL masks the count)
    std::uint32_t larger = levelBytes * (scale - 1) * 4 / (scale * 3);
    return larger + extraSize;
}
EI_REPLACE_METHOD(0x006908E0, MmpHeader::MipOffset);

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

// --- CTexture ------------------------------------------------------------------------------------

int CTexture::CreateFromMmp(CTextureDevice* device, const void* data, unsigned mip) {
    if (flags_ & kTextureLoaded) return 1;
    const auto* header = static_cast<const MmpHeader*>(data);
    header->MipOffset(mip);   // the result is not used

    std::uint32_t width = header->width >> (mip & 31);
    std::uint32_t height = header->height >> (mip & 31);
    std::uint32_t levels = header->mipCount - mip;
    if (static_cast<const CTextureManager*>(owner_)->limitTo512_) {
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
    // (the original: a system-memory staging surface filled by 0068FA10, loaded into a managed
    // texture with the kind's priority hints, its priority set to 100).
    CDXSurface* decoded = DecodedHolder(device, this, data, mip, levels);
    CDXSurface* previous = surface_;
    surface_ = decoded;
    ++decoded->refCount;
    if (previous) previous->Release();   // 004E6BA0
    uploadResult_ = static_cast<int>(header->FilledBytes(format_, mip, levels));
    MarkLoaded(this, header);
    extra_.SetSize(static_cast<int>(header->extraSize));
    std::memcpy(extra_.data, header + 1, header->extraSize);
    return 1;
}
EI_REPLACE_METHOD(0x00690430, CTexture::CreateFromMmp);

// --- CTextureManager -----------------------------------------------------------------------------

int CTextureManager::LoadMmpExtra(const char* name, AlignedBuffer* out) {
    MmpHeader header;
    std::memset(&header, 0, sizeof(header));
    mfc::CString path;
    mfc::CFile file;
    path.Format(game::Text(0x00795DCC), DataPath(), name);   // "%stextures\\%s.mmp"
    if (file.Open(path, mfc::CFile::modeRead, nullptr)) {
        file.Read(&header, sizeof(header));
        int size = static_cast<int>(header.extraSize);
        out->SetSizeDiscard(size);
        if (size) file.Read(out->data, static_cast<unsigned>(size));
        file.Close();
        return 1;
    }
    path.Format(game::Text(0x00795F44), name, game::Text(0x0078FB4C));   // "%s%s", ".mmp"
    void* data = GetFile(path, nullptr);
    if (!data) {
        out->SetSizeDiscard(0);
        return 0;
    }
    const auto* fileHeader = static_cast<const MmpHeader*>(data);
    out->SetSizeDiscard(static_cast<int>(fileHeader->extraSize));
    if (fileHeader->extraSize) std::memcpy(out->data, fileHeader + 1, fileHeader->extraSize);
    CloseFile(data);
    return 1;
}
EI_REPLACE_METHOD(0x006928C0, CTextureManager::LoadMmpExtra);
