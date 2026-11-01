#pragma once

// Helpers shared by world_effect.cpp and world_effect_visual.cpp: the code the effects call, the
// replicated-field operations, and the x87 details the effects repeat.

#include "world_effect.h"
#include "map_object.h"
#include "game_strings.h"
#include "world.h"
#include "world_data.h"
#include "net_delta.h"
#include "game_singletons.h"

#include "lib/float_select.h"
#include "lib/nan_audit.h"

#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>

namespace effect {

// --- Globals -------------------------------------------------------------------------------------

// The light and particle managers are g_clientWorld.terrain, g_clientWorld.lightFactory and g_clientWorld.particleManager (game_singletons.h).


inline std::uint8_t FrameCounter() { return static_cast<std::uint8_t>(g_serverWorld.tick); }

// Constants read from memory, as the original does, so that nothing is folded.
inline float ConstAt(const float& constant) { return *reinterpret_cast<const volatile float*>(&constant); }

// By reference: a float passed by value would be loaded and stored through the FPU.
inline std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    u = std::bit_cast<std::uint32_t>(f);
    return u;
}
inline float FromBits(std::uint32_t u) {
    float f;
    f = std::bit_cast<float>(u);
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> inline float FromBits(T) = delete;
// The inline test after every velocity change: moving unless all three components are 0.
inline bool IsMoving(const Vector3& v) {
    EI_NAN_AUDIT(v.x, v.y, v.z);
    return !(v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
}

// `min(value, 1.0)` the way the scorch code selects it: the value when it compares below 1.0 or
// is NaN, else 1.0.
inline float BelowOneOrOne(const float& value) {
    return MinKeepA(value, 1.0f);
}

// --- Engine objects ------------------------------------------------------------------------------

// --- Replicated fields (net_delta.cpp) -----------------------------------------------------------

// The four streams of a field emptied.
inline void EmptyField(DeltaField* f) {
    f->snapshot[0].Resize(0);
    f->snapshot[1].Resize(0);
    f->out[0].Resize(0);
    f->out[1].Resize(0);
}

// Every Unpack ends by checking that the snapshot was read to its end (Position, then Size).
inline bool SnapshotReadToEnd() {
    MemoryStream* s = delta::g_readSnapshot;
    std::uint32_t position = s->Position();
    return position == s->Size();
}

// --- Save streams (the effects' Load / Save slots) -------------------------------------------------

inline void Read(ByteStream* stream, void* data, std::uint32_t size) { stream->Read(data, size); }
inline void Write(ByteStream* stream, const void* data, std::uint32_t size) { stream->Write(data, size); }
inline int Version(const ByteStream* stream) { return static_cast<int>(stream->version); }
} // namespace effect
