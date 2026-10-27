// The vertical blank switch's rules (docs/notes/renderer.md section 15a), apart from the window module so
// that tools/perf_check links them without the renderer.
#include "render_internal.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dxgi1_5.h>

namespace render::detail {

// The vertical blank switch (docs/notes/renderer.md section 15): on, sync interval 1 and no flags; off, interval
// 0 and, where the system supports it (IDXGIFactory5, DXGI_FEATURE_PRESENT_ALLOW_TEARING), a swap chain made with
// DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING (`allowTearing`: SetVsync(false), not the harness's SetWaitForVsync) whose presents carry DXGI_PRESENT_ALLOW_TEARING (the only way a windowed flip-model
// chain runs unlocked). Present's tearing flag needs the chain's, so it follows the flags the chain was made with.
bool TearingSupported(IDXGIFactory2* factory) {
    IDXGIFactory5* factory5 = nullptr;
    BOOL allowed = FALSE;
    if (factory && SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5)))) {
        if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowed, sizeof(allowed)))) allowed = FALSE;
        factory5->Release();
    }
    return allowed != FALSE;
}
UINT ChainFlagsFor(bool allowTearing, bool tearingSupported) {
    return allowTearing && tearingSupported ? static_cast<UINT>(DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) : 0u;
}
UINT SyncIntervalFor(bool vsync) { return vsync ? 1u : 0u; }
UINT PresentFlagsFor(bool vsync, UINT chainFlags) {
    return !vsync && (chainFlags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) ? static_cast<UINT>(DXGI_PRESENT_ALLOW_TEARING) : 0u;
}

} // namespace render::detail
