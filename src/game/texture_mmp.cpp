// .mmp textures: creating a texture's Direct3D surfaces from the file data (00690430), the mip
// offsets (006908E0) and the file's extra block (006928C0).
//
// In the switch-over ("present" with "cpuwindows", ticket R8) an .mmp texture has no DirectDraw
// texture: the new renderer decodes the levels the game would load from the file data (keyed by the
// holder, render::TextureFromMmp) and the holder has no surface and no memory
// (NewDecodedTextureHolder, dx_surface.cpp); no staging surface, fill (0068FA10), Load or priority.
// The byte size the texture cache counts is what the fill would have returned
// (MmpHeader::FilledBytes). A format the new renderer does not decode keeps its DirectDraw texture.
// See docs/notes/renderer.md (section 16).
#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "render/render.h"
#include "dx_surface.h"
#include "originals_render.h"

#include "../hook/log.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

const char* DataPath() { return *reinterpret_cast<const char* const*>(0x007AF460); }
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// Nonzero when mipmaps are used (otherwise every texture gets a single level).
int MipmapsEnabled() { return *reinterpret_cast<const int*>(0x0079BCE0); }
// The lock around Direct3D device calls.
CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// COM slots.
constexpr int kDeviceLoad = 43;            // IDirect3DDevice7::Load (+0xAC)
constexpr int kSurfaceSetPriority = 45;    // IDirectDrawSurface7::SetPriority (+0xB4)
template <typename... Args> long Com(void* object, int slot, Args... args) {
    void* fn = (*static_cast<void***>(object))[slot];
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>(fn)(object, args...);
}

// 006908E0 through its game address (so its verify wrapper sees the call).
__declspec(naked) std::uint32_t __fastcall CallMipOffset(const MmpHeader*, void*, unsigned) EI_ORIGINAL(0x006908E0)

// A new surface for `desc`, or null when the allocation fails.
CDXSurface* NewSurface(CTextureDevice* device, const std::uint32_t* desc) {
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    return memory ? static_cast<CDXSurface*>(DXSurfaceFromDescOriginal(memory, nullptr, device, desc)) : nullptr;
}

// Drops a reference; the last one deletes the surface (slot 0, the deleting destructor).
void ReleaseSurface(CDXSurface* surface) {
    if (--surface->refCount == 0) ei::VCall<void*>(surface, 0, 1u);
}

// DDSURFACEDESC2 (0x7C bytes) as dwords.
enum DescIndex { kSize = 0, kFlags = 1, kHeight = 2, kWidth = 3, kMipCount = 6, kPixelFormat = 18, kCaps = 26,
                 kCaps2 = 27, kCaps3 = 28, kCaps4 = 29, kDescWords = 31 };
constexpr std::uint32_t kDescFlags = 0x1007;          // CAPS | HEIGHT | WIDTH | PIXELFORMAT
constexpr std::uint32_t kDescMipMapCount = 0x20000;   // MIPMAPCOUNT
constexpr std::uint32_t kCapsMipMaps = 0x400008;      // COMPLEX | MIPMAP

// The description both surfaces share: size, pixel format, `levels` mipmaps when more than one.
void FillDescription(std::uint32_t (&desc)[kDescWords], const CTexture* texture, std::uint32_t width,
                     std::uint32_t height, std::uint32_t levels, std::uint32_t caps, std::uint32_t caps2) {
    std::memset(desc, 0, sizeof(desc));
    desc[kHeight] = height;
    desc[kSize] = sizeof(desc);
    desc[kFlags] = kDescFlags;
    desc[kWidth] = width;
    std::memcpy(&desc[kPixelFormat], &texture->format_, sizeof(PixelFormat));
    desc[kCaps] = caps;
    desc[kCaps2] = caps2;
    if (levels > 1) {
        desc[kMipCount] = levels;
        desc[kFlags] |= kDescMipMapCount;
        desc[kCaps] = caps | kCapsMipMaps;
    }
}

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

// In the switch-over ("present" with "cpuwindows", ticket R8): a holder with no DirectDraw texture
// for the levels the new renderer decoded from the file (keyed by the holder). Null outside the
// switch-over, or when the new renderer did not decode them (then the DirectDraw texture as before).
CDXSurface* DecodedHolder(CTextureDevice* device, const CTexture* texture, const void* data, unsigned mip,
                          unsigned levels) {
    CDXSurface* holder = NewDecodedTextureHolder(device, static_cast<int>(texture->width_),
                                                 static_cast<int>(texture->height_), texture->format_.words);
    if (!holder) return nullptr;
    if (render::TextureFromMmp(holder, data, mip, levels)) return holder;
    if (!g_undecodedLogged) {
        g_undecodedLogged = true;
        ei::Log("cpuwindows: an .mmp texture the new renderer does not decode (format %08X) gets a DirectDraw texture",
                static_cast<const MmpHeader*>(data)->format);
    }
    ++holder->refCount;
    holder->Release();   // (004EFD20 counts it out again)
    return nullptr;
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
    CallMipOffset(header, nullptr, mip);   // the result is not used

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

    // In the switch-over the new renderer decodes the levels from the file and the holder has no
    // DirectDraw texture: no staging surface, fill, Load or priority.
    if (CDXSurface* decoded = DecodedHolder(device, this, data, mip, levels)) {
        CDXSurface* previous = surface_;
        surface_ = decoded;
        ++decoded->refCount;
        if (previous) ReleaseSurface(previous);
        uploadResult_ = static_cast<int>(header->FilledBytes(format_, mip, levels));
        MarkLoaded(this, header);
        extra_.SetSize(static_cast<int>(header->extraSize));
        std::memcpy(extra_.data, header + 1, header->extraSize);
        return 1;
    }

    // A system-memory surface that receives the file's pixels.
    std::uint32_t desc[kDescWords];
    FillDescription(desc, this, width, height, levels, 0x1800, 0);   // TEXTURE | SYSTEMMEMORY
    CDXSurface* staging = NewSurface(device, desc);
    if (staging) ++staging->refCount;

    // The texture surface: driver-managed, with the texture's priority hints.
    FillDescription(desc, this, width_, height_, levels, 0x11000, 0x20000);   // TEXTURE | ALLOCONLOAD; TEXTUREMANAGE
    if (flags_ & 0x40)
        desc[kCaps2] = 0x20080;
    else if (flags_ & 0x80)
        desc[kCaps2] = 0x20008;
    else if (flags_ & 0x300)
        desc[kCaps2] = 0x20004;
    CDXSurface* surface = NewSurface(device, desc);
    CDXSurface* previous = surface_;
    surface_ = surface;
    if (surface) ++surface->refCount;
    if (previous) ReleaseSurface(previous);   // 004E6BA0

    uploadResult_ = CallFillSurface(staging, data, static_cast<int>(mip));
    gameapi::EnterCriticalSection(DeviceLock());
    device->lastLoadResult_ = Com(device->d3dDevice_, kDeviceLoad, surface_->surface, static_cast<void*>(nullptr),
                                  staging->surface, static_cast<void*>(nullptr), 0u);
    gameapi::LeaveCriticalSection(DeviceLock());

    MarkLoaded(this, header);
    Com(surface_->surface, kSurfaceSetPriority, 100u);

    extra_.SetSize(static_cast<int>(header->extraSize));
    std::memcpy(extra_.data, header + 1, header->extraSize);

    if (staging) ReleaseSurface(staging);
    // The new renderer (src/render) decodes the same levels from the file data.
    if (render::Active() && surface_) render::TextureFromMmp(surface_, data, mip, levels);
    return 1;
}
EI_REPLACE_METHOD(0x00690430, CTexture::CreateFromMmp);

// --- CTextureManager -----------------------------------------------------------------------------

int CTextureManager::LoadMmpExtra(const char* name, AlignedBuffer* out) {
    MmpHeader header;
    std::memset(&header, 0, sizeof(header));
    mfc::CString path;
    mfc::CFile file;
    path.Format(Text(0x00795DCC), DataPath(), name);   // "%stextures\\%s.mmp"
    if (file.Open(path, mfc::CFile::modeRead, nullptr)) {
        file.Read(&header, sizeof(header));
        int size = static_cast<int>(header.extraSize);
        out->SetSizeDiscard(size);
        if (size) file.Read(out->data, static_cast<unsigned>(size));
        file.Close();
        return 1;
    }
    path.Format(Text(0x00795F44), name, Text(0x0078FB4C));   // "%s%s", ".mmp"
    void* data = ei::VCall<void*>(this, 19, static_cast<const char*>(path), static_cast<std::uint32_t*>(nullptr));
    if (!data) {
        out->SetSizeDiscard(0);
        return 0;
    }
    const auto* fileHeader = static_cast<const MmpHeader*>(data);
    out->SetSizeDiscard(static_cast<int>(fileHeader->extraSize));
    if (fileHeader->extraSize) std::memcpy(out->data, fileHeader + 1, fileHeader->extraSize);
    ei::VCall<int>(this, 20, data);
    return 1;
}
EI_REPLACE_METHOD(0x006928C0, CTextureManager::LoadMmpExtra);
