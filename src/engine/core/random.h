#pragma once

#include "lib/scales.h"

#include <cstdint>

// The game's random number generator: Bob Jenkins' ISAAC (RANDSIZL = 8), the reference
// implementation's randctx with one extra leading field. Used for AI, combat and loot rolls, so it
// must stay bit-exact. 0x814 bytes.
class IsaacRandom {
public:
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

    // Next() * (max - min) * 2^-32 + min, rounded as the original rounds it (RangeOfWord).
    float Range(float min, float max);
    // Next() * limit * 2^-32: a draw in [0, limit) (ScaleWord).
    float Below(float limit);

    // Once (while seeded is 0): seeds results (Seed), then the reference randinit(ctx, TRUE) and the first Refill.
    void Init();
    // Once (while seeded is 0): Init with `source`'s memory as the seed instead of Seed's.
    void InitFrom(const IsaacRandom& source);

    // Fills results with 1 KB from the system's random source and seeds the CRT's rand() from it (random.cpp); the
    // harness's seed hook instead, when set.
    void Seed();

    std::uint32_t seeded = 0;       // non-zero once seeded from results
    std::uint32_t count = 0;        // results left (randcnt)
    std::uint32_t results[256] = {};  // randrsl
    std::uint32_t memory[256] = {};  // randmem
    std::uint32_t aa = 0, bb = 0, cc = 0;  // randa, randb, randc

private:
    // Init after the seed: the reference randinit(ctx, TRUE) over results and the first Refill.
    void MixSeed();
};

// A random word as a float.
inline float WordToFloat(std::uint32_t word) { return static_cast<float>(word); }

// A random word scaled to [0, limit): limit * word * 2^-32 with one rounding after the product (the scale is a power
// of two, so the same value as `word * (limit * 2^-32)`).
inline float ScaleWord(std::uint32_t word, float limit) { return (limit * WordToFloat(word)) * lib::kWordScale; }

// ScaleWord to [min, max) from `min`: what Range gives for the word.
inline float RangeOfWord(std::uint32_t word, float min, float max) { return ScaleWord(word, max - min) + min; }

// The game's generator (random.cpp).
extern IsaacRandom g_random;

// The client effects' generator: the draws whose results only reach the picture or the sound (the terrain's
// animated tiles and ambient sounds, particles, lightning shapes, sound picks, decals, the interface's
// flourishes) and whose count follows frames, the camera or what was drawn. The game drew them from g_random or
// rand(); apart, the game's draws no longer depend on them. Seeded from g_random's state when that is seeded
// (World::Create); before that it draws from the zero state, as g_random does unseeded.
extern IsaacRandom g_clientRandom;

// The particle sources' generator (particle_*.cpp): client draws of their own, so a change to an effect moves only the
// particles, not the other client draws (idle animations, scorch marks, sounds). Seeded from g_clientRandom's state
// right after it (World::Create).
extern IsaacRandom g_particleRandom;

// A draw of g_clientRandom in rand()'s range (0..0x7FFF), for the client effects that drew rand().
int ClientRand();
// ClientRand() * n / 0x7FFF % n, as libcrt::RandBelow scales rand() (n > 0).
inline int ClientRandBelow(int n) { return ClientRand() * n / 0x7FFF % n; }
