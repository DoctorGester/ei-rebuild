// A window surface's draw (CUiSurface vtable slot 0, 005FDA80, __fastcall on the surface; called by
// CUiWindow's surface pass 005FECC0 and the interface's draw 005F5320 for the backdrop and the frame
// rate surface): when the surface exists and is shown (+0x0C bit 4), the part of its screen
// rectangle (+0x24) inside the screen (the interface's +0x00 width, +0x04 height) is blitted from
// the surface's DirectDraw surface (the holder +0x10 -> +0x10) onto the render target (the holder's
// device object +0x0C -> +0x18): with the matching source rectangle, or the whole surface stretched
// when bit 2 is set; with the source colour key +0x34 when bit 1 is set. Nothing when the surface is
// the render target itself. The new renderer (src/render) takes the same blit; with "nointerface"
// the old blit is skipped, as it is for a holder without a DirectDraw surface (made in the
// switch-over: its pixels are only in the new renderer's memory). See docs/notes/renderer.md.
#include "ei/hook.h"
#include "lib/gameapi.h"
#include "render/render.h"
#include "originals_render.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(object) + offset);
}

constexpr int kBlt = 5;                                  // IDirectDrawSurface7::Blt
constexpr unsigned long kBltPlain = 0x1000200;           // DDBLT_WAIT | DDBLT_ASYNC
constexpr unsigned long kBltKeyed = 0x1010200;           // ... | DDBLT_KEYSRCOVERRIDE

template <typename... Args>
long ComCall(void* object, int slot, Args... args) {
    return reinterpret_cast<long(__stdcall*)(void*, Args...)>((*static_cast<void***>(object))[slot])(object, args...);
}

CRITICAL_SECTION* DeviceLock() { return reinterpret_cast<CRITICAL_SECTION*>(0x007AEE78); }

// 004F0B00 (cdecl): reports a DirectDraw error with a text.

const char* const kBlitFailed = reinterpret_cast<const char*>(0x0079326C);   // "Can't blit I3D surface to screen"

// DDBLTFX (100 bytes): only the size and the source colour key are used.
struct BltFx {
    std::uint32_t size;
    std::uint32_t unused[22];
    std::uint32_t sourceKeyLow;    // +0x5C ddckSrcColorkey
    std::uint32_t sourceKeyHigh;
};
static_assert(sizeof(BltFx) == 100);

} // namespace

// 005FDA80.
void __fastcall UiSurfaceBlit(char* surface) {
    void* holder = At<void*>(surface, 0x10);
    std::uint32_t flags = At<std::uint32_t>(surface, 0x0C);
    if (!holder || !(flags & 4)) return;
    const int* screenSize = At<int*>(surface, 0x04);   // the interface: width, height
    const RECT& rect = At<RECT>(surface, 0x24);
    RECT clipped{0, 0, screenSize[0], screenSize[1]};
    gameapi::IntersectRect(&clipped, &clipped, &rect);
    if (gameapi::IsRectEmpty(&clipped)) return;
    RECT source{clipped.left - rect.left, clipped.top - rect.top, 0, 0};
    source.right = source.left + (clipped.right - clipped.left);
    source.bottom = source.top + (clipped.bottom - clipped.top);
    RECT* sourceRect = (flags & 2) ? nullptr : &source;
    bool keyed = (flags & 1) != 0;
    std::uint32_t key = At<std::uint32_t>(surface, 0x34);

    void* target = At<void*>(At<void*>(holder, 0x0C), 0x18);   // the device object's render target
    void* targetSurface = At<void*>(target, 0x10);
    void* sourceSurface = At<void*>(holder, 0x10);
    if (targetSurface == sourceSurface) return;

    if (render::Active()) {
        render::Rect destination{clipped.left, clipped.top, clipped.right, clipped.bottom};
        render::Rect part{source.left, source.top, source.right, source.bottom};
        render::SurfaceBlit(holder, destination, sourceRect ? &part : nullptr, keyed, key);
    }
    // The old path's blit (a holder made in the switch-over has no DirectDraw surface to blit).
    if (!render::DrawOldInterface() || !sourceSurface) return;

    if (keyed) {
        BltFx fx{};
        fx.size = 100;
        fx.sourceKeyLow = key;
        fx.sourceKeyHigh = key;
        gameapi::EnterCriticalSection(DeviceLock());
        ComCall(targetSurface, kBlt, &clipped, sourceSurface, sourceRect, kBltKeyed, &fx);
        gameapi::LeaveCriticalSection(DeviceLock());
        return;
    }
    gameapi::EnterCriticalSection(DeviceLock());
    long result = ComCall(targetSurface, kBlt, &clipped, sourceSurface, sourceRect, kBltPlain, static_cast<void*>(nullptr));
    gameapi::LeaveCriticalSection(DeviceLock());
    if (result != 0) ReportError(result, kBlitFailed);
}
EI_REPLACE(0x005FDA80, UiSurfaceBlit);
