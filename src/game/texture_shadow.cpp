// The texture manager's shadow textures: the 64x64 texture of each object's shadow (00692010) and
// the shared render surface the shadows are rasterised into (00692140), with the lock and unlock
// 0051CE60 writes the render surface through (00690FC0 / 00691030) and its copy of the render
// surface into the shadow's texture (LoadPixelsFrom; inline in 0051CE60).
//
// Both have no DirectDraw texture (ticket R8): their holders have only the new renderer's memory
// (CreateBlank -> NewMemoryTextureHolder), the lock describes that memory (no Lock / Unlock), the copy
// (the original: the device's Load) is a CPU copy of its rows, and the new renderer reads the shadow
// texture's memory when a draw binds it (004EA4A0 through 0051CE60). See docs/notes/renderer.md
// (section 7).
#include "app.h"
#include "texture.h"

#include "dx_surface.h"
#include "game_strings.h"
#include "lib/crt.h"
#include "originals_engine.h"

#include "ei/hook.h"
#include "lib/gameapi.h"

#include "../hook/log.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace {


// The shadow textures: CreateObject's kind, 64 pixels square, the device's 4:4:4:4 format (its
// texture kind 0x21 in the missing-format report).
constexpr int kShadowKind = 0x25;
constexpr std::uint32_t kShadowSize = 0x40;
constexpr int kFormat4444 = 0x4444;
constexpr int kKind4444 = 0x21;
constexpr std::uint32_t kMmpMagic = 0x504D4D;   // "MMP"

// CEngineObject::flags_ of a texture: its kind bits (texture_blank.cpp): 0x200 a shadow texture,
// 0x400 the render surface (in system memory).
constexpr std::uint32_t kShadowTextureKeep = 0xFFFFFE3F, kShadowTextureFlag = 0x200;
constexpr std::uint32_t kRenderSurfaceKeep = 0xFFFFFC3F, kRenderSurfaceFlag = 0x400;

bool g_mixedLogged = false;

// The header both make their surface from: 64x64, one level, 4:4:4:4 (16 bits).
MmpHeader ShadowHeader() {
    MmpHeader h;
    std::memset(&h, 0, sizeof(h));
    h.alphaKind = 4;
    h.redBits = 4;
    h.greenBits = 4;
    h.greenShift = 4;
    h.blueBits = 4;
    h.alphaMask = 0xF000;
    h.alphaShift = 12;
    h.redMask = 0xF00;
    h.redShift = 8;
    h.greenMask = 0xF0;
    h.blueMask = 0xF;
    h.blueShift = 0;
    h.bitsPerPixel = 16;
    h.format = kFormat4444;
    h.magic = kMmpMagic;
    h.width = kShadowSize;
    h.height = kShadowSize;
    h.mipCount = 1;
    return h;
}

// A new texture of the shadows' size and format with the kind bits `keep` / `flag`, its surface
// made by CreateBlank (only the new renderer's memory). Null when the surface cannot be made (the
// texture released again); with no 4:4:4:4 format FatalError's result, which the callers return as
// their texture (`*missing` set).
CTexture* MakeShadowTexture(CTextureManager* manager, std::uint32_t keep, std::uint32_t flag, bool* missing) {
    *missing = false;
    auto* texture = static_cast<CTexture*>(manager->CreateObject(kShadowKind, -1));
    MmpHeader header = ShadowHeader();
    texture->width_ = kShadowSize;
    texture->height_ = kShadowSize;
    PixelFormat format;
    if (!manager->device_->FindFormat(kFormat4444, &format)) {
        *missing = true;
        return reinterpret_cast<CTexture*>(
            static_cast<std::uintptr_t>(game::FatalError(game::CantFindTextureFormatText(), kKind4444)));
    }
    texture->format_ = format;
    texture->flags_ = (texture->flags_ & keep) | flag;
    if (!texture->CreateBlank(manager->device_, &header)) {   // (never: it returns 1)
        texture->Release();
        return nullptr;
    }
    return texture;
}

} // namespace

// --- 00692010 / 00692140 ---------------------------------------------------------------------------

CTexture* CTextureManager::NewShadowTexture() {
    bool missing;
    return MakeShadowTexture(this, kShadowTextureKeep, kShadowTextureFlag, &missing);
}
EI_REWRITTEN(0x00692010, CTextureManager::NewShadowTexture);

CTexture* CTextureManager::ShadowRenderSurface() {
    if (shadowRenderSurface_) return shadowRenderSurface_;
    bool missing;
    CTexture* texture = MakeShadowTexture(this, kRenderSurfaceKeep, kRenderSurfaceFlag, &missing);
    if (missing || !texture) return texture;
    CTexture* previous = shadowRenderSurface_;
    shadowRenderSurface_ = texture;
    ++texture->refCount_;
    if (previous) previous->Release();
    return shadowRenderSurface_;
}
EI_REWRITTEN(0x00692140, CTextureManager::ShadowRenderSurface);

// --- 00690FC0 / 00691030 ---------------------------------------------------------------------------

// The holder's pixels are the new renderer's memory: described, no Lock / Unlock, the result 0 in
// the holder.
int CTexture::LockPixels(SurfacePixels* pixels) {
    CDXSurface* holder = surface_;
    if (!holder) return 0;
    gameapi::EnterCriticalSection(TheDeviceLock());
    holder->result = 0;
    return DirectPixels(holder, pixels) ? 1 : 0;   // (always memory: CreateBlank made it)
}

void CTexture::UnlockPixels() {
    CDXSurface* holder = surface_;
    if (!holder) return;
    gameapi::LeaveCriticalSection(TheDeviceLock());
    holder->result = 0;
}

// --- 0051CE60's copy -------------------------------------------------------------------------------

void CTexture::LoadPixelsFrom(CTextureDevice* device, const CTexture* source) {
    const CDXSurface* to = surface_;
    const CDXSurface* from = source->surface_;
    // Both in the new renderer's memory (the same size and format), copied row by row.
    device->lastLoadResult_ = 0;
    SurfacePixels target, pixels;
    if (!DirectPixels(to, &target) || !DirectPixels(from, &pixels)) {
        // (Never: CreateBlank gives both memory or throws.)
        if (!g_mixedLogged) {
            g_mixedLogged = true;
            ei::Log("cpuwindows: a shadow texture or its render surface has no memory; the shadow is not copied");
        }
        return;
    }
    const int rows = target.height < pixels.height ? target.height : pixels.height;
    const int width = target.width < pixels.width ? target.width : pixels.width;
    const std::size_t bytes = static_cast<std::size_t>(width) * (target.bitsPerPixel / 8);
    for (int y = 0; y < rows; ++y) std::memcpy(target.bits + y * target.pitch, pixels.bits + y * pixels.pitch, bytes);
}
