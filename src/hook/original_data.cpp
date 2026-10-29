// The data seam (game/game_data.h) in ei_harness.dll, which runs only in game.exe: game.exe's .rdata and .data are at
// their own addresses there, so the shift is 0 and no address is stray. The image's moved copy:
// src/standalone/image_data.cpp.
#include "game/game_data.h"

#include <cstdlib>

namespace ei {

std::uintptr_t g_dataShift = 0;

// GameAddress asks only with a shift.
void StrayGameAddress(std::uintptr_t) { std::abort(); }

} // namespace ei
