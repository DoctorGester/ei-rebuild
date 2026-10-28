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

#include "ei/hook.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <concepts>
#include <cstdint>
#include <cstring>

namespace effect {

// --- Globals -------------------------------------------------------------------------------------

// The light and particle managers are TheTerrain(), TheLightFactory() and TheParticleManager() (game_singletons.h).
inline CLightningManager* BoltFactory() { return TheClientWorld()->listManager_; }   // CreateObject makes bolts
inline int ChangeStamp() { return TheServerWorld()->updateCount_; }
inline std::uint8_t FrameCounter() { return static_cast<std::uint8_t>(CurrentWorldTick()); }

// Constants read from memory, as the original does, so that nothing is folded.
inline float ConstAt(const float& constant) { return *reinterpret_cast<const volatile float*>(&constant); }

// By reference: a float passed by value would be loaded and stored through the FPU.
inline std::uint32_t Bits(const float& f) {
    std::uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
inline float FromBits(std::uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> inline float FromBits(T) = delete;
// The inline test after every velocity change: moving unless all three components are 0.
inline int IsMoving(const Vector3& v) {
    EI_NAN_AUDIT(v.x, v.y, v.z);
    return v.x == 0.0f && v.y == 0.0f && v.z == 0.0f ? 0 : 1;
}

// `min(value, 1.0)` the way the scorch code selects it: the value when it compares below 1.0 or
// is NaN, else 1.0.
inline float BelowOneOrOne(const float& value) {
    return value >= 1.0f ? 1.0f : value;
}

// --- Engine objects ------------------------------------------------------------------------------

// The world an object belongs to (the effects' manager context is the world, too).
inline CWorld* WorldOf(const CEngineObject* object) { return ObjectWorld(object); }
inline CWorld* ContextWorld(const CEngineObject* object) { return object->WorldContext(); }

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
    BitStream* s = delta::g_readSnapshot;
    std::uint32_t position = s->Position();
    return position == s->Size();
}

// --- Save streams (the effects' Load / Save slots) -------------------------------------------------

inline void Read(CStream* stream, void* data, std::uint32_t size) { stream->Read(data, size); }
inline void Write(CStream* stream, const void* data, std::uint32_t size) { stream->Write(data, size); }
inline int Version(const CStream* stream) { return static_cast<int>(stream->version_); }
// A dword written through a stack copy, which is how the original writes most scalars.
inline void WriteCopy(CStream* stream, const void* field) {
    std::uint32_t copy;
    std::memcpy(&copy, field, 4);
    Write(stream, &copy, 4);
}

// --- MFC strings --------------------------------------------------------------------------------

inline const char* EmptyCString() { return game::EmptyText(); }

} // namespace effect
