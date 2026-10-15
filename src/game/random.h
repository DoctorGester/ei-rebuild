#pragma once

#include <cstdint>

// The game's random number generator: Bob Jenkins' ISAAC (RANDSIZL = 8), the reference
// implementation's randctx with one extra leading field. Used for AI, combat and loot rolls, so it
// must stay bit-exact. 0x814 bytes.
class IsaacRandom {
public:
    // The game's generator (007A9D40).
    static IsaacRandom& Global() { return *reinterpret_cast<IsaacRandom*>(std::uintptr_t(0x007A9D40)); }

    // 004667F0. Next 32-bit random value.
    std::uint32_t Next();

    // 0049EE70. Generates the next 256 results (reference isaac()).
    void Refill();

    // 00488970 (random.cpp). Next() * (max - min) * ~2^-32 + min, left unrounded in ST0.
    float Range(float min, float max);

    // 0049F150. Once (while seeded_ is 0): seeds results_ (0049F660: 1 KB of a random file on C:\,
    // mixed with rand()), then the reference randinit(ctx, TRUE) and the first Refill.
    void Init();

    // 0049F660. Fills results_ with seed material: 1 KB read at a random offset (0x201..0x400) of a
    // file of at least 2 KB picked at random on C:\ (FindSeedFile), each word then xored with
    // rand(); retried until a read gave 1 KB that is not all zero.
    void Seed();
    // 0049F4B0 (RET 0x10; `this` not used). Walks `pattern` (a directory plus "\*.*") depth first,
    // skipping dots, system and hidden entries and counting files in *counter; the first file of at
    // least 0x800 bytes once *counter has reached `target` is copied to `out` (returns 1). 0 when
    // the walk ends first.
    int FindSeedFile(char* out, const char* pattern, int target, int* counter);

    std::uint32_t seeded_;          // +0x000 non-zero once seeded from results_
    std::uint32_t count_;           // +0x004 results left (randcnt)
    std::uint32_t results_[256];    // +0x008 randrsl
    std::uint32_t memory_[256];     // +0x408 randmem
    std::uint32_t a_, b_, c_;       // +0x808 randa, randb, randc
};
static_assert(sizeof(IsaacRandom) == 0x814);
