#pragma once

// Helpers shared by the particle callback files (particle_callbacks*.cpp): x87 rounding, the game's constants
// and random generator, and the helpers they call. Include after effects.h. Floating point follows the
// original's x87 code: `double` for values the original keeps on the FPU stack, static_cast<float> where it
// stores to a float; constants the original reads from memory are read from the exe at run time.

#include "effects.h"
#include "map_object.h"
#include "random.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "game_singletons.h"
#include "lib/float_select.h"

#include <cstdint>
#include <cstring>

namespace particle {

inline double D(float f) { return f; }

// A zero-extended dword as a double (exact).
inline double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// A float read back from memory (the original stores, then reloads it).
inline float Load(const float& f) { return *static_cast<const volatile float*>(&f); }


inline void SetBits(void* field, std::uint32_t bits) { lib::StoreBytes(field, bits); }
inline std::uint32_t Bits(const void* field) {
    std::uint32_t bits;
    lib::LoadBytes(bits, field);
    return bits;
}

inline std::uint32_t Next() { return g_random.Next(); }

// IsaacRandom::Range: leaves the result unrounded.
inline double Range(float min, float max) { return g_random.Range(min, max); }

// CWorldClient::RawSurfaceHeightAt(x, y) on the client world (unrounded).
inline double GroundHeight(float x, float y) { return g_clientWorld.RawSurfaceHeightAt(x, y); }

// The game's debug message (one argument; compiled out, other mods may hook it).
inline void DebugMessage(const char* a0) { game::DebugLog(a0); }


// The attached object as a map object: the callbacks call its slots past CObject3D's (the
// carriers are units).
inline CMapObject* Carrier(CObject3D* object) { return static_cast<CMapObject*>(object); }
// The attached object's radius (units: the unit's size).
inline double UnitScale(CObject3D* object) { return Carrier(object)->Radius(); }

#define EI_P_ONE 1.0f
#define EI_P_ZERO 0.0f
#define EI_P_SCALE_2 4.656612874161595e-10   // ~2^-31
#define EI_P_SCALE_1 2.3283064370807974e-10   // ~2^-32

// Next() * 2^-31 - 1: a random value in [-1, 1), rounded to float.
inline float SignedUnit() { return static_cast<float>(Unsigned(Next()) * EI_P_SCALE_2 - D(EI_P_ONE)); }

} // namespace particle
