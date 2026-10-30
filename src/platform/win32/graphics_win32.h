#pragma once

// The Direct3D backend's present rules (graphics_win32.cpp), which tools/perf_check checks on real swap chains.
#include <cstdint>

namespace platform::graphics {

// The vertical blank switch (docs/notes/renderer.md section 15a): the swap chain's flags, Present's sync interval and
// Present's flags.
std::uint32_t ChainFlagsFor(bool allowTearing, bool tearingSupported);
std::uint32_t SyncIntervalFor(bool vsync);
std::uint32_t PresentFlagsFor(bool vsync, std::uint32_t chainFlags);

}   // namespace platform::graphics
