#pragma once

#include <cstdint>

// The game's random number generator: Bob Jenkins' ISAAC (RANDSIZL = 8), the reference
// implementation's randctx with one extra leading field. Used for AI, combat and loot rolls, so it
// must stay bit-exact. 0x814 bytes.
class IsaacRandom {
public:
    // The game's generator (random.cpp).
    static IsaacRandom& Global();

    // Next 32-bit random value.
    std::uint32_t Next();

    // The harness's seams (tests/verify): a seed that replaces Seed's (its deterministic runs), and an observer of
    // every Next draw (its random trace; `caller` is Next's return address). Null: the game's own behaviour.
    using SeedHook = void (*)(IsaacRandom& random);
    using DrawObserver = void (*)(IsaacRandom& random, std::uint32_t value, const void* caller);
    static void SetSeedHook(SeedHook hook);
    static void SetDrawObserver(DrawObserver observer);

    // Generates the next 256 results (reference isaac()).
    void Refill();

    // Next() * (max - min) * ~2^-32 + min, left unrounded in ST0 (random.cpp).
    float Range(float min, float max);

    // Once (while seeded_ is 0): seeds results_ (1 KB of a random file on C:\,
    // mixed with rand()), then the reference randinit(ctx, TRUE) and the first Refill.
    void Init();

    // Fills results_ with seed material: 1 KB read at a random offset (0x201..0x400) of a
    // file of at least 2 KB picked at random on C:\ (FindSeedFile), each word then xored with
    // rand(); retried until a read gave 1 KB that is not all zero. The harness's seed hook instead, when set.
    void Seed();
    // (`this` not used). Walks `pattern` (a directory plus "\*.*") depth first, skipping dots, system and
    // hidden entries and counting files in *counter; the first file of at least 0x800 bytes once *counter has
    // reached `target` is copied to `out` (returns 1). 0 when the walk ends first.
    int FindSeedFile(char* out, const char* pattern, int target, int* counter);

    std::uint32_t seeded_;          // non-zero once seeded from results_
    std::uint32_t count_;           // results left (randcnt)
    std::uint32_t results_[256];    // randrsl
    std::uint32_t memory_[256];     // randmem
    std::uint32_t a_, b_, c_;       // randa, randb, randc
};
static_assert(sizeof(IsaacRandom) == 0x814);
