#pragma once

// Helpers shared by world_effect.cpp and world_effect_visual.cpp: the code the effects call, the
// replicated-field operations.

#include "world_effect.h"
#include "map_object.h"
#include "game_strings.h"
#include "world.h"
#include "world_data.h"
#include "net_delta.h"
#include "game_singletons.h"

#include "lib/float_select.h"

#include <cstdint>
#include <cstring>

namespace effect {

// --- Globals -------------------------------------------------------------------------------------

// The light and particle managers are g_clientWorld.terrain, g_clientWorld.lightFactory and g_clientWorld.particleManager (game_singletons.h).


inline std::uint8_t FrameCounter() { return static_cast<std::uint8_t>(g_serverWorld.tick); }

// The inline test after every velocity change: moving unless all three components are 0.
inline bool IsMoving(const Vector3& v) {
    return !(v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
}

// `min(value, 1.0)` the way the scorch code selects it: the value when it compares below 1.0 or
// is NaN, else 1.0.
inline float BelowOneOrOne(const float& value) {
    return MinKeepA(value, 1.0f);
}

// --- Engine objects ------------------------------------------------------------------------------

// --- Save streams (the effects' Load / Save slots) -------------------------------------------------

inline void Read(ByteStream* stream, void* data, std::uint32_t size) { stream->Read(data, size); }
inline void Write(ByteStream* stream, const void* data, std::uint32_t size) { stream->Write(data, size); }
} // namespace effect
