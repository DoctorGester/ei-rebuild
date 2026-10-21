#pragma once

// The game globals the new renderer reads, in one place (an offline test can point them at its own
// data). Everything else it reads hangs off these or comes in as arguments. The defaults are the
// original's addresses; Where() holds them resolved through the data seam (game_data.h).

#include "game/game_data.h"

#include <cstdint>

namespace render::game {

struct Addresses {
    std::uintptr_t window = ei::DataId(0x0079BC28);         // HWND of the game window
    std::uintptr_t renderer = ei::DataId(0x007AF060);       // the renderer object (vtable 0073D93C): +0x14 clear colour
    std::uintptr_t deviceObject = ei::DataId(0x007AF080);   // pointer to the device object: +0x18 render target (CDXSurface:
                                                // +0x14 width, +0x18 height), +0x54 IDirect3DDevice7
    std::uintptr_t deviceLock = ei::DataId(0x007AEE78);     // CRITICAL_SECTION held around DirectDraw / Direct3D calls
};

Addresses& Where();

} // namespace render::game
