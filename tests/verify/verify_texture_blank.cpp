// Checks for the texture manager's CPU-written textures (texture_blank.cpp) and the helpers that let
// an original write a texture whose holder has only the new renderer's memory (memory_texture.h).
//
// Both functions make DirectDraw objects (or the new renderer's memory) and register a texture with
// the manager: ours only.
#include "verify.h"
#include "memory_texture.h"

#include "../../src/game/dx_surface.h"
#include "../../src/game/texture.h"
#include "../../src/hook/log.h"
#include "../../src/render/render.h"

#include <windows.h>
#include <ddraw.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }
// The memory's rows into the surface (`toSurface`) or back. False when the surface cannot be locked.
bool CopyRows(IDirectDrawSurface7* surface, const std::uint32_t* memory, bool toSurface) {
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    ::EnterCriticalSection(DeviceLock());
    bool ok = surface->Lock(nullptr, &desc, DDLOCK_WAIT | (toSurface ? DDLOCK_WRITEONLY : DDLOCK_READONLY), nullptr) ==
              DD_OK;
    if (ok) {
        auto* bits = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(memory[9]));
        std::uint32_t pitch = memory[4];
        std::uint32_t rowBytes = memory[3] * (memory[0x15] / 8);
        if (rowBytes > static_cast<std::uint32_t>(desc.lPitch)) rowBytes = static_cast<std::uint32_t>(desc.lPitch);
        std::uint32_t rows = memory[2] < desc.dwHeight ? memory[2] : desc.dwHeight;
        auto* lock = static_cast<std::uint8_t*>(desc.lpSurface);
        for (std::uint32_t y = 0; y < rows; ++y) {
            if (toSurface)
                std::memcpy(lock + y * desc.lPitch, bits + y * pitch, rowBytes);
            else
                std::memcpy(bits + y * pitch, lock + y * desc.lPitch, rowBytes);
        }
        surface->Unlock(nullptr);
    }
    ::LeaveCriticalSection(DeviceLock());
    return ok;
}

struct Lent {
    CDXSurface* holder;
    void* surface;
};
std::vector<Lent>* g_lent = nullptr;   // while a LendNewTextures is open
int g_lendFailures = 0;

} // namespace

namespace memory_texture {

CDXSurface* HolderOf(const void* texture) {
    return texture ? static_cast<const CTexture*>(texture)->surface_ : nullptr;
}

bool MemoryOnly(const CDXSurface* holder) { return holder && !holder->surface && render::SurfaceMemoryOf(holder); }

bool Lend(CDXSurface* holder) {
    std::uint32_t memory[0x1F];
    if (!MemoryOnly(holder) || !holder->device || !holder->device->ddraw_ || !ClientPixels(holder, memory)) return false;
    DDSURFACEDESC2 desc{};
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = memory[3];
    desc.dwHeight = memory[2];
    std::memcpy(&desc.ddpfPixelFormat, &memory[18], sizeof(DDPIXELFORMAT));
    desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_SYSTEMMEMORY;
    auto* ddraw = static_cast<IDirectDraw7*>(holder->device->ddraw_);
    IDirectDrawSurface7* surface = nullptr;
    ::EnterCriticalSection(DeviceLock());
    HRESULT hr = ddraw->CreateSurface(&desc, &surface, nullptr);
    ::LeaveCriticalSection(DeviceLock());
    if (FAILED(hr) || !surface || !CopyRows(surface, memory, true)) {
        if (surface) surface->Release();
        if (g_lendFailures++ < 5)
            ei::Log("verify: no DirectDraw texture %lux%lu (%lu bits) for an original to write (%08lX): ours only",
                    desc.dwWidth, desc.dwHeight, desc.ddpfPixelFormat.dwRGBBitCount, static_cast<unsigned long>(hr));
        return false;
    }
    holder->surface = surface;
    return true;
}

void TakeBack(CDXSurface* holder, bool keep) {
    if (!holder || !holder->surface) return;
    auto* surface = static_cast<IDirectDrawSurface7*>(holder->surface);
    std::uint32_t memory[0x1F];
    if (keep && ClientPixels(holder, memory)) CopyRows(surface, memory, false);
    surface->Release();
    holder->surface = nullptr;
}

LendNewTextures::LendNewTextures() {
    if (!g_lent) g_lent = new std::vector<Lent>();
}

LendNewTextures::~LendNewTextures() {
    std::vector<Lent>* lent = g_lent;
    g_lent = nullptr;
    for (const Lent& l : *lent) {
        // A texture the original released again has lost its holder (and the surface with it).
        if (render::SurfaceMemoryOf(l.holder) && l.holder->surface == l.surface) TakeBack(l.holder, true);
    }
    delete lent;
}

void LendNewTextures::Made(CDXSurface* holder) {
    if (g_lent && Lend(holder)) g_lent->push_back(Lent{holder, holder->surface});
}

const char* BlankTextureProblem(CTextureManager* manager, const CTexture* texture, int width, int height, int formatId,
                                int holdersBefore) {
    if (!texture) return "no texture";
    if (texture->width_ != static_cast<std::uint32_t>(width) || texture->height_ != static_cast<std::uint32_t>(height))
        return "size";
    PixelFormat format{};
    if (!manager->device_->FindFormat(formatId, &format) || std::memcmp(&format, &texture->format_, sizeof(format)) != 0)
        return "format";
    const CDXSurface* holder = texture->surface_;
    if (!holder || holder->refCount < 1 || holder->device != manager->device_) return "holder";
    if (holder->width != width || holder->height != width || holder->bitsPerPixel != static_cast<int>(format.words[3]))
        return "holder size";
    if (texture->uploadResult_ != 0 || texture->block_) return "byte size / block";
    if (HolderCount() != holdersBefore + 1) return "holder count";
    const render::SurfaceMemory* memory = render::SurfaceMemoryOf(holder);
    if (render::Presenting() && render::CpuWindows()) {
        if (holder->surface) return "a DirectDraw texture in the switch-over";
        if (!memory || memory->width != width || memory->height != width || memory->bitsPerPixel != holder->bitsPerPixel)
            return "no memory of its size";
        std::uint32_t alpha = (format.words[1] & DDPF_ALPHAPIXELS) ? format.words[7] : 0;
        if (memory->redMask != format.words[4] || memory->greenMask != format.words[5] ||
            memory->blueMask != format.words[6] || memory->alphaMask != alpha)
            return "memory format";
    } else if (!holder->surface || memory) {
        return "not a plain DirectDraw texture";
    }
    return nullptr;
}

int HolderCount() { return *reinterpret_cast<const int*>(0x007AEE94); }

} // namespace memory_texture

// --- 0068F6C0 ----------------------------------------------------------------------------------------

// Ours only (it makes a DirectDraw texture): 1, a holder with a DirectDraw surface of header->width
// square in the texture's format, no block left, the byte size 0 (the original's). Not reached any
// more: its three callers (00691D80, the shadow textures 00692010 / 00692140) are rewritten and call
// its body (MakeBlank) directly; their wrappers check the same (verify_texture_shadow.cpp).
static void* s_createBlank;
static int __fastcall CreateBlankVerify(CTexture* self, void*, CTextureDevice* device, const MmpHeader* header) {
    int result = self->CreateBlank(device, header);
    const CDXSurface* holder = self->surface_;
    bool ok = result == 1 && holder && holder->surface && holder->refCount >= 1 && holder->device == device &&
              holder->width == static_cast<int>(header->width) && holder->height == static_cast<int>(header->width) &&
              holder->bitsPerPixel == static_cast<int>(self->format_.words[3]) && self->uploadResult_ == 0 &&
              !self->block_;
    ei::verify::Check("CTexture::CreateBlank", true, ok, "%u mips %u flags %08X: result %d holder %p (%dx%d, %d bits)",
                      header->width, header->mipCount, self->flags_, result, static_cast<const void*>(holder),
                      holder ? holder->width : 0, holder ? holder->height : 0, holder ? holder->bitsPerPixel : 0);
    return result;
}
EI_VERIFY(0x0068F6C0, CreateBlankVerify, 0, s_createBlank);

// --- 00691D80 ----------------------------------------------------------------------------------------

namespace {

int FormatIdOfKind(int kind) {
    switch (kind) {
    case 0x1E: return 0x5650;
    case 0x20: return 0x5551;
    case 0x21: return 0x4444;
    default: return 0;
    }
}

} // namespace

// Ours only (it registers a texture with the manager). For the kinds the game uses (0x1E, 0x20,
// 0x21): the texture of the asked size in the device's format for the kind, a holder of `width`
// square in that format (the holder count one higher), the byte size 0; in the switch-over with
// "cpuwindows" only the new renderer's memory (no DirectDraw texture), otherwise a DirectDraw texture
// and no memory. Then lent a surface when an original that writes it runs (LendNewTextures).
// Scenarios: ui_saveload (the save / load pictures, 0x1E), ui_camp / ui_screens (the global map's
// weather 0x21, item pictures 0x20), render_present / render_present_cpuwindows (the switch-over).
static void* s_newTexture;
static CTexture* __fastcall NewTextureVerify(CTextureManager* self, void*, int width, int height, int kind) {
    int holders = memory_texture::HolderCount();
    CTexture* texture = self->NewTexture(width, height, kind);
    const char* problem = nullptr;
    if (FormatIdOfKind(kind)) {
        problem = memory_texture::BlankTextureProblem(self, texture, width, height, FormatIdOfKind(kind), holders);
        ei::verify::Check("CTextureManager::NewTexture", true, problem == nullptr, "%dx%d kind %X: %s", width, height,
                          kind, problem ? problem : "ok");
    }
    // (A missing format returns FatalError's result, not a texture.)
    if (texture && !problem) memory_texture::LendNewTextures::Made(texture->surface_);
    return texture;
}
EI_VERIFY(0x00691D80, NewTextureVerify, 0, s_newTexture);
