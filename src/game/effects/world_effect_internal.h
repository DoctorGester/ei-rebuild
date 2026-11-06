#pragma once

// Helpers shared by world_effect.cpp and world_effect_visual.cpp: the code the effects call, the
// replicated-field operations.

#include "game/effects/world_effect.h"
#include "game/objects/map_object.h"
#include "game/world/world.h"
#include "game/net/net_delta.h"
#include "game/app/singletons.h"

#include "lib/float_select.h"

#include <cstdint>
#include <cstring>

namespace effect {

// --- Globals -------------------------------------------------------------------------------------

// The light and particle managers are g_clientWorld.terrain, g_clientWorld.lightFactory and g_clientWorld.particleManager (app/singletons.h).


inline std::uint8_t FrameCounter() { return static_cast<std::uint8_t>(g_serverWorld.tick); }

// The inline test after every velocity change: moving unless all three components are 0.
inline bool IsMoving(const Vector3& v) {
    return !(v.x == 0.0f && v.y == 0.0f && v.z == 0.0f);
}

// The tail of a sound effect's delta: volume, radius, velocity and the no-fade flag, then the position read first
// is taken and the effect starts (0 when the snapshot is not consumed).
template <class Sound>
int UnpackSoundTail(Sound& sound, DeltaReader& field, const Vector3& position, int update) {
    sound.volume = field.ReadFloat16();
    sound.radius = field.ReadFloat16();
    sound.velocity = field.ReadFloat16x3();
    field.ReadDelta(&sound.noFade, 1);
    sound.moving = IsMoving(sound.velocity);
    if (!field.SnapshotConsumed()) return 0;
    sound.localPosition = position;
    sound.Start(update);
    return 1;
}

// What every Reset does to the base part first: position, field streams, start tick, velocity, moving flag and stamp.
inline void ClearBase(Effect* e) {
    e->localPosition = Vector3{0.0f, 0.0f, 0.0f};
    e->state.Empty();
    e->velocity = Vector3{0.0f, 0.0f, 0.0f};
    e->startTick = 0;
    e->moving = 0;
    e->changeStamp = -1;
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
