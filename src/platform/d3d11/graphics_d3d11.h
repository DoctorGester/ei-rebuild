#pragma once

// The Direct3D 11 backend's own part (graphics_d3d11.cpp) beyond graphics.h: the vertical blank switch's rules that
// tools/perf_check checks on real swap chains, and the harness build's removable device.

#include "platform/graphics.h"

#include <cstdint>

struct IDXGIFactory2;

namespace platform::graphics {

// ---- The vertical blank switch (docs/notes/renderer.md section 15a) -----------------------------------------------

// On: sync interval 1 and no flags. Off: interval 0 and, where the system supports it, a swap chain made with the
// tearing flag (only for PresentMode::allowTearing) whose presents carry it: the only way a windowed flip-model chain
// runs unlocked. Present's tearing flag needs the chain's, so it follows the flags the chain was made with.
bool TearingSupported(IDXGIFactory2* factory);
std::uint32_t ChainFlagsFor(bool allowTearing, bool tearingSupported);
std::uint32_t SyncIntervalFor(bool vsync);
std::uint32_t PresentFlagsFor(bool vsync, std::uint32_t chainFlags);

// ---- The harness build's removable device (docs/notes/renderer.md, "a real removal of the device") ----------------

// The next Start makes the device through D3D11On12 on a Direct3D 12 device, which RequestRemoval removes for real
// (ID3D12Device5::RemoveDevice, this process only) right before the next present; false when the device is not one.
void MakeRemovable();
bool Removable();
bool RequestRemoval();
int Removals();

} // namespace platform::graphics
