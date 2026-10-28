#pragma once

// Helpers shared by the particle callback files (particle_callbacks*.cpp): x87 rounding, the game's constants
// and random generator, and the helpers they call. Include after effects.h. Floating point follows the
// original's x87 code: `double` for values the original keeps on the FPU stack, static_cast<float> where it
// stores to a float; constants the original reads from memory are read from the exe at run time.

#include "effects.h"
#include "map_object.h"
#include "random.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace particle {

inline double D(float f) { return f; }

// A zero-extended dword loaded with FILD qword (exact).
inline double Unsigned(std::uint32_t value) {
    volatile std::int64_t wide = value;
    return static_cast<double>(wide);
}

// A float read back from memory (the original stores, then reloads it).
inline float Load(const float& f) { return *static_cast<const volatile float*>(&f); }


inline void SetBits(void* field, std::uint32_t bits) { std::memcpy(field, &bits, sizeof(bits)); }
inline std::uint32_t Bits(const void* field) {
    std::uint32_t bits;
    std::memcpy(&bits, field, sizeof(bits));
    return bits;
}

inline std::uint32_t Next() { return IsaacRandom::Global().Next(); }

// IsaacRandom::Range: leaves the result unrounded in ST0.
inline double Range(float min, float max) { return IsaacRandom::Global().Range(min, max); }

// CWorldClient::RawSurfaceHeightAt(x, y) on the client world (ST0).
inline double GroundHeight(float x, float y) { return TheClientWorld()->RawSurfaceHeightAt(x, y); }

// x87 "FCOMP; FNSTSW; SHL 0x17; SAR 0x1F" selects: C0 (a < b or unordered) picks `a`.
inline float PickIfLess(float a, float b) { return a >= b ? b : a; }

// The game's debug message (cdecl, one argument; compiled out, other mods may hook it).
inline void __cdecl DebugMessage(const char* a0) { game::DebugLog(a0); }

// The element array's inlined SetSize(n) (MFC CArray; 32-byte aligned blocks).
inline void SetElementCount(CParticleSource* s, int n) {
    if (n == 0) {
        if (s->elements) {
            lib::FreeAligned32(s->elements);
            s->elements = nullptr;
        }
        s->elementCapacity = 0;
        s->elementCount = 0;
    } else if (!s->elements) {
        s->elements = AllocateElements(n);
        s->elementCapacity = n;
        s->elementCount = n;
    } else if (n <= s->elementCapacity) {
        s->elementCount = n;
    } else {
        int grow = s->elementGrowBy;
        if (grow == 0) {
            grow = s->elementCount / 8;
            if (grow < 4) grow = 4;
            else if (grow > 1024) grow = 1024;
        }
        int capacity = s->elementCapacity + grow;
        if (!(capacity > n)) capacity = n;
        ParticleElement* data = AllocateElements(capacity);
        std::memcpy(data, s->elements, static_cast<std::size_t>(s->elementCount) * sizeof(ParticleElement));
        lib::FreeAligned32(s->elements);
        s->elements = data;
        s->elementCount = n;
        s->elementCapacity = capacity;
    }
}


// The attached object as a map object: the callbacks call its slots past CObject3D's (the
// carriers are units).
inline CMapObject* Carrier(CObject3D* object) { return static_cast<CMapObject*>(object); }
// The attached object's class and its radius (units: the unit's size).
inline int ClassOf(CObject3D* object) { return object->GetClassId(); }
inline double UnitScale(CObject3D* object) { return Carrier(object)->Radius(); }

constexpr int kUnitClass = 0x50;

#define EI_P_ONE 1.0f /*0073B84C*/
#define EI_P_ZERO 0.0f /*0073B858*/
#define EI_P_SCALE_2 4.656612874161595e-10 /*0073C508*/     // ~2^-31
#define EI_P_SCALE_1 2.3283064370807974e-10 /*0073C468*/     // ~2^-32

// Next() * 2^-31 - 1: a random value in [-1, 1), rounded to float.
inline float SignedUnit() { return static_cast<float>(Unsigned(Next()) * EI_P_SCALE_2 - D(EI_P_ONE)); }

} // namespace particle
