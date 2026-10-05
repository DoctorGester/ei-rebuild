// Checks for the texture manager's shadow textures (texture_shadow.cpp). All four make, lock or
// unlock DirectDraw textures (or the new renderer's memory) and cannot run twice: ours only. Their
// only caller is the object shadow 0051CE60 (through their game addresses), reached by every
// scenario with object shadows on the old path's settings: render_scene, frames_basic,
// render_present and render_present_cpuwindows (the switch-over: memory-only shadow textures).
#include "verify.h"
#include "memory_texture.h"

#include "../../src/game/dx_surface.h"
#include "../../src/game/texture.h"
#include "../../src/render/render.h"

#include <windows.h>

#include <cstdint>

namespace {

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// How often this thread holds the device lock.
LONG LockDepth() {
    CRITICAL_SECTION* lock = DeviceLock();
    if (reinterpret_cast<std::uintptr_t>(lock->OwningThread) != ::GetCurrentThreadId()) return 0;
    return lock->RecursionCount;
}

constexpr int kShadowSize = 0x40;
constexpr int kFormat4444 = 0x4444;
constexpr std::uint32_t kKindBits = 0x7C0;   // the texture kind flags (texture_blank.cpp)

bool HasFormat(CTextureManager* manager) {
    PixelFormat format{};
    return manager->device_->FindFormat(kFormat4444, &format) != 0;
}

// The shadow texture's checks (BlankTextureProblem) and its kind flag.
const char* ShadowProblem(CTextureManager* manager, const CTexture* texture, int holders, std::uint32_t kindFlag) {
    const char* problem = memory_texture::BlankTextureProblem(manager, texture, kShadowSize, kShadowSize, kFormat4444,
                                                              holders);
    if (problem) return problem;
    if ((texture->flags_ & kKindBits) != kindFlag) return "kind flags";
    return nullptr;
}

} // namespace

// --- 00692010 ----------------------------------------------------------------------------------------

// Ours only (it registers a texture): a 64x64 texture in the device's 4:4:4:4 format with the kind
// flag 0x200 (and none of 0x40..0x100, 0x400), a holder of its size (the holder count one higher),
// the byte size 0; in the switch-over with "cpuwindows" only the new renderer's memory, otherwise a
// DirectDraw texture. (Without a 4:4:4:4 format it ends in a fatal error: not checked.)
static void* s_newShadowTexture;
static CTexture* __fastcall NewShadowTextureVerify(CTextureManager* self) {
    int holders = memory_texture::HolderCount();
    CTexture* texture = self->NewShadowTexture();
    if (HasFormat(self)) {
        const char* problem = ShadowProblem(self, texture, holders, 0x200);
        ei::verify::Check("CTextureManager::NewShadowTexture", true, problem == nullptr, "%s",
                          problem ? problem : "ok");
    }
    return texture;
}
EI_VERIFY(0x00692010, NewShadowTextureVerify, 0, s_newShadowTexture);

// --- 00692140 ----------------------------------------------------------------------------------------

// Ours only (it registers a texture once): the first call as 00692010's checks with the kind flag
// 0x400, the texture kept (counted) in +0xE0; every later call returns that texture and makes no
// holder.
static void* s_shadowRenderSurface;
static CTexture* __fastcall ShadowRenderSurfaceVerify(CTextureManager* self) {
    CTexture* before = self->shadowRenderSurface_;
    int holders = memory_texture::HolderCount();
    CTexture* texture = self->ShadowRenderSurface();
    const char* problem = nullptr;
    if (before) {
        if (texture != before || memory_texture::HolderCount() != holders) problem = "not the kept texture";
    } else if (HasFormat(self)) {
        problem = ShadowProblem(self, texture, holders, 0x400);
        if (!problem && (self->shadowRenderSurface_ != texture || texture->refCount_ < 1)) problem = "not kept";
    }
    ei::verify::Check("CTextureManager::ShadowRenderSurface", true, problem == nullptr, "%s", problem ? problem : "ok");
    return texture;
}
EI_VERIFY(0x00692140, ShadowRenderSurfaceVerify, 0, s_shadowRenderSurface);

// --- 00690FC0 / 00691030 -----------------------------------------------------------------------------

// Ours only (a surface cannot be locked twice). 1, the device lock held once more by this thread,
// the description of the holder's size and bits per pixel with its pixels and a pitch for a row;
// for a holder without a DirectDraw surface (the switch-over) the new renderer's memory. 0 without
// a holder (the lock not taken).
static void* s_lockPixels;
static int __fastcall LockPixelsVerify(CTexture* self, void*, std::uint32_t* desc) {
    LONG depth = LockDepth();
    int result = self->LockPixels(desc);
    const CDXSurface* holder = self->surface_;
    bool ok;
    if (!holder) {
        ok = result == 0 && LockDepth() == depth;
    } else {
        // (A failed Lock ends in a fatal error.)
        ok = result == 1 && LockDepth() == depth + 1 && desc[9] != 0 && static_cast<int>(desc[3]) == holder->width &&
             static_cast<int>(desc[2]) == holder->height && static_cast<int>(desc[21]) == holder->bitsPerPixel &&
             static_cast<int>(desc[4]) >= holder->width * holder->bitsPerPixel / 8;
        if (ok && !holder->surface) {
            const render::SurfaceMemory* memory = render::SurfaceMemoryOf(holder);
            ok = memory && desc[9] == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(memory->bits));
        }
    }
    ei::verify::Check("CTexture::LockPixels", true, ok, "holder %p (surface %p): result %d, %ux%u pitch %u",
                      static_cast<const void*>(holder), holder ? holder->surface : nullptr, result, desc[3], desc[2],
                      desc[4]);
    return result;
}
EI_VERIFY(0x00690FC0, LockPixelsVerify, 0, s_lockPixels);

// Ours only: with a holder the device lock is left once (and the holder's result is 0: a failed
// Unlock ends in a fatal error); without one nothing changes.
static void* s_unlockPixels;
static void __fastcall UnlockPixelsVerify(CTexture* self) {
    LONG depth = LockDepth();
    self->UnlockPixels();
    const CDXSurface* holder = self->surface_;
    bool ok = holder ? LockDepth() == depth - 1 && holder->result == 0 : LockDepth() == depth;
    ei::verify::Check("CTexture::UnlockPixels", true, ok, "holder %p", static_cast<const void*>(holder));
}
EI_VERIFY(0x00691030, UnlockPixelsVerify, 0, s_unlockPixels);
