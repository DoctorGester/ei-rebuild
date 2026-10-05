// .mmp textures: creating a texture's Direct3D surfaces from the file data (00690430), the mip
// offsets (006908E0) and the file's extra block (006928C0).
#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "render/render.h"
#include "originals_render.h"

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

// 0068FA10: fills `surface` with the .mmp pixels from level `mip` on (thiscall on the texture).
__declspec(naked) int __fastcall FillSurface(CTexture*, void*, CDXSurface*, const void*, unsigned)
    EI_ORIGINAL(0x0068FA10)
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

// --- AlignedBuffer -----------------------------------------------------------------------------

std::uint8_t* AllocateAligned(int size) {   // also 004ECB50
    auto* raw = static_cast<std::uint8_t*>(gamecrt::operator_new(static_cast<std::size_t>(size + 0x24)));
    auto* data = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(raw) + 0x23) & ~std::uintptr_t{0x1F});
    reinterpret_cast<std::uint8_t**>(data)[-1] = raw;
    return data;
}

void FreeAligned(std::uint8_t* data) { gamecrt::operator_delete(reinterpret_cast<std::uint8_t**>(data)[-1]); }

// The new capacity for `size` bytes: capacity + growBy (an eighth of the size, 4..1024, when
// growBy is 0), at least `size`.
int GrownCapacity(const AlignedBuffer& buffer, int size) {
    int grow = buffer.growBy;
    if (grow == 0) {
        grow = buffer.size / 8;
        if (grow < 4)
            grow = 4;
        else if (grow > 0x400)
            grow = 0x400;
    }
    int capacity = buffer.capacity + grow;
    return size < capacity ? capacity : size;
}

// CByteArray::SetSize as CreateFromMmp inlines it: 0 frees the data, growing keeps the contents.
void ResizeKeep(AlignedBuffer& buffer, int size) {
    if (size == 0) {
        if (buffer.data) {
            FreeAligned(buffer.data);
            buffer.data = nullptr;
        }
        buffer.capacity = 0;
        buffer.size = 0;
    } else if (!buffer.data) {
        buffer.data = AllocateAligned(size);
        buffer.capacity = size;
        buffer.size = size;
    } else if (size <= buffer.capacity) {
        buffer.size = size;
    } else {
        int capacity = GrownCapacity(buffer, size);
        std::uint8_t* data = AllocateAligned(capacity);
        std::memcpy(data, buffer.data, static_cast<std::size_t>(static_cast<std::uint32_t>(buffer.size)));
        FreeAligned(buffer.data);
        buffer.data = data;
        buffer.size = size;
        buffer.capacity = capacity;
    }
}

// The variant LoadMmpExtra inlines: the contents are not kept and size 0 keeps the block.
void ResizeDiscard(AlignedBuffer& buffer, int size) {
    if (!buffer.data) {
        buffer.data = AllocateAligned(size);
        buffer.capacity = size;
        buffer.size = size;
    } else if (size <= buffer.capacity) {
        buffer.size = size;
    } else {
        int capacity = GrownCapacity(buffer, size);
        std::uint8_t* data = AllocateAligned(capacity);
        FreeAligned(buffer.data);
        buffer.data = data;
        buffer.capacity = capacity;
        buffer.size = size;
    }
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

    // A system-memory surface that receives the file's pixels.
    std::uint32_t desc[kDescWords];
    height_ = height;
    width_ = width;
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

    uploadResult_ = FillSurface(this, nullptr, staging, data, mip);
    gameapi::EnterCriticalSection(DeviceLock());
    device->lastLoadResult_ = Com(device->d3dDevice_, kDeviceLoad, surface_->surface, static_cast<void*>(nullptr),
                                  staging->surface, static_cast<void*>(nullptr), 0u);
    gameapi::LeaveCriticalSection(DeviceLock());

    // Alpha kind from the file.
    flags_ &= 0xFFFC3FFF;
    switch (header->alphaKind) {
    case 0: flags_ |= 0x4000; break;
    case 1: flags_ |= 0x8000; break;
    case 4: flags_ |= 0x10000; break;
    case 8: flags_ |= 0x20000; break;
    default: break;
    }
    flags_ |= kTextureLoaded;
    Com(surface_->surface, kSurfaceSetPriority, 100u);

    ResizeKeep(extra_, static_cast<int>(header->extraSize));
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
        ResizeDiscard(*out, size);
        if (size) file.Read(out->data, static_cast<unsigned>(size));
        file.Close();
        return 1;
    }
    path.Format(Text(0x00795F44), name, Text(0x0078FB4C));   // "%s%s", ".mmp"
    void* data = ei::VCall<void*>(this, 19, static_cast<const char*>(path), static_cast<std::uint32_t*>(nullptr));
    if (!data) {
        ResizeDiscard(*out, 0);
        return 0;
    }
    const auto* fileHeader = static_cast<const MmpHeader*>(data);
    ResizeDiscard(*out, static_cast<int>(fileHeader->extraSize));
    if (fileHeader->extraSize) std::memcpy(out->data, fileHeader + 1, fileHeader->extraSize);
    ei::VCall<int>(this, 20, data);
    return 1;
}
EI_REPLACE_METHOD(0x006928C0, CTextureManager::LoadMmpExtra);
