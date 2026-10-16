#pragma once

// Helpers shared by the particle callback files (particle_callbacks*.cpp): x87 rounding, the game's
// constants and random generator, and the forwarders they call. Include after effects.h.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float; constants the original reads from memory
// are read from the exe at run time.

#include "effects.h"
#include "random.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "originals_engine.h"
#include "originals_world.h"

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

inline float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
inline double GameDouble(std::uintptr_t address) { return *reinterpret_cast<const volatile double*>(address); }
inline std::uint32_t GameDword(std::uintptr_t address) {
    return *reinterpret_cast<const volatile std::uint32_t*>(address);
}

inline void SetBits(void* field, std::uint32_t bits) { std::memcpy(field, &bits, sizeof(bits)); }
inline std::uint32_t Bits(const void* field) {
    std::uint32_t bits;
    std::memcpy(&bits, field, sizeof(bits));
    return bits;
}

inline IsaacRandom& GlobalRandom() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }
inline std::uint32_t Next() { return GlobalRandom().Next(); }

// 00488970 IsaacRandom::Range: leaves the result unrounded in ST0.
inline double Range(float min, float max) { return RandomRange(&GlobalRandom(), nullptr, min, max); }

// 00564070 TerrainOwner::SurfaceHeightRaw(x, y) on the terrain owner 007AF5B8; leaves ST0 unrounded.
inline double GroundHeight(float x, float y) { return SurfaceHeightRaw(reinterpret_cast<void*>(0x007AF5B8), nullptr, x, y); }

// x87 "FCOMP; FNSTSW; SHL 0x17; SAR 0x1F" selects: C0 (a < b or unordered) picks `a`.
inline float PickIfLess(float a, float b) { return !(a >= b) ? a : b; }

// 0044C470: the game's debug message (cdecl, one argument; compiled out, other mods may hook it).
inline void __cdecl DebugMessage(const char* a0) { game::DebugLog(a0); }

// 00497EB0 AllocateElements through its address (the original calls it there).

// The element array's inlined SetSize(n) (MFC CArray: data +0x154, size +0x158, max +0x15C,
// grow by +0x160; 32-byte aligned blocks from 00497EB0).
inline void SetElementCount(CParticleSource* s, int n) {
    if (n == 0) {
        if (s->elements) {
            gamecrt::operator_delete(reinterpret_cast<void**>(s->elements)[-1]);
            s->elements = nullptr;
        }
        s->elementCapacity = 0;
        s->elementCount = 0;
    } else if (!s->elements) {
        s->elements = AllocateElementsAt(n);
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
        ParticleElement* data = AllocateElementsAt(capacity);
        std::memcpy(data, s->elements, static_cast<std::size_t>(s->elementCount) * sizeof(ParticleElement));
        gamecrt::operator_delete(reinterpret_cast<void**>(s->elements)[-1]);
        s->elements = data;
        s->elementCount = n;
        s->elementCapacity = capacity;
    }
}

// Element fields by byte offset (their meaning depends on the source type).
inline float* ElementFloat(ParticleElement* e, int offset) {
    return reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(e) + offset);
}
inline std::int32_t* ElementInt(ParticleElement* e, int offset) {
    return reinterpret_cast<std::int32_t*>(reinterpret_cast<std::uint8_t*>(e) + offset);
}
inline float* ParticleFloat(Particle* p, int offset) {
    return reinterpret_cast<float*>(reinterpret_cast<std::uint8_t*>(p) + offset);
}

// The attached object's class (vtable slot 12) and its slot 60 (a float: the unit's size).
inline int ClassOf(void* object) {
    using Fn = int(__fastcall*)(void*, void*);
    return (*static_cast<Fn**>(object))[12](object, nullptr);
}
inline double UnitScale(void* object) {
    using Fn = double(__fastcall*)(void*, void*);
    return (*static_cast<Fn**>(object))[60](object, nullptr);
}

constexpr int kUnitClass = 0x50;

#define EI_P_ONE ::particle::GameFloat(0x0073B84C)          // 1.0
#define EI_P_ZERO ::particle::GameFloat(0x0073B858)         // 0.0
#define EI_P_SCALE_2 ::particle::GameDouble(0x0073C508)     // ~2^-31
#define EI_P_SCALE_1 ::particle::GameDouble(0x0073C468)     // ~2^-32

// Next() * 2^-31 - 1: a random value in [-1, 1), rounded to float.
inline float SignedUnit() { return static_cast<float>(Unsigned(Next()) * EI_P_SCALE_2 - D(EI_P_ONE)); }

} // namespace particle
