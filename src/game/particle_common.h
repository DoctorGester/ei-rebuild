#pragma once

// Helpers shared by the particle callback files (particle_callbacks*.cpp): the game's constants, its random
// generator and the helpers they call. Include after effects.h. Plain float maths (the files build for SSE2).

#include "effects.h"
#include "map_object.h"
#include "random.h"
#include "world.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/float_select.h"

#include <cstdint>
#include <cstring>

namespace particle {

// A random word as a float (scaled by the callers' EI_P_SCALE_* to a fraction).
inline float Unsigned(std::uint32_t value) { return static_cast<float>(value); }

inline void SetBits(void* field, std::uint32_t bits) { lib::StoreBytes(field, bits); }
inline std::uint32_t Bits(const void* field) {
    std::uint32_t bits;
    lib::LoadBytes(bits, field);
    return bits;
}

inline std::uint32_t Next() { return g_clientRandom.Next(); }

inline float Range(float min, float max) { return g_clientRandom.Range(min, max); }

// WorldClient::RawSurfaceHeightAt(x, y) on the client world.
inline float GroundHeight(float x, float y) { return static_cast<float>(g_clientWorld.RawSurfaceHeightAt(x, y)); }

// The attached object as a map object: the callbacks call its slots past CObject3D's (the
// carriers are units).
inline CMapObject* Carrier(CObject3D* object) { return static_cast<CMapObject*>(object); }
// The attached object's radius (units: the unit's size).
inline float UnitScale(CObject3D* object) { return Carrier(object)->Radius(); }

#define EI_P_ONE 1.0f
#define EI_P_ZERO 0.0f
#define EI_P_SCALE_2 4.656613e-10f   // ~2^-31
#define EI_P_SCALE_1 2.3283064e-10f   // ~2^-32

// Next() * 2^-31 - 1: a random value in [-1, 1).
inline float SignedUnit() { return Unsigned(Next()) * EI_P_SCALE_2 - EI_P_ONE; }

} // namespace particle
