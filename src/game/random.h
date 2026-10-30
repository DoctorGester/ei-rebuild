#pragma once

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

    // Next() * (max - min) * 2^-32 + min, rounded as the original rounds it (random.cpp).
    float Range(float min, float max);

    // Once (while seeded is 0): seeds results (Seed), then the reference randinit(ctx, TRUE) and the first Refill.
    void Init();
    // Once (while seeded is 0): Init with `source`'s memory as the seed instead of Seed's.
    void InitFrom(const IsaacRandom& source);

    // Fills results with 1 KB from the system's random source and seeds the CRT's rand() from it (random.cpp); the
    // harness's seed hook instead, when set.
    void Seed();

    std::uint32_t seeded;           // non-zero once seeded from results
    std::uint32_t count;            // results left (randcnt)
    std::uint32_t results[256];     // randrsl
    std::uint32_t memory[256];      // randmem
    std::uint32_t aa, bb, cc;       // randa, randb, randc

private:
    // Init after the seed: the reference randinit(ctx, TRUE) over results and the first Refill.
    void MixSeed();
};
static_assert(sizeof(IsaacRandom) == 0x814);

// A random word as a float (the callers scale it by 2^-32 to a fraction).
inline float WordToFloat(std::uint32_t word) { return static_cast<float>(word); }

// The game's generator (random.cpp).
extern IsaacRandom g_random;

// The client effects' generator: the draws whose results only reach the picture or the sound (the terrain's
// animated tiles and ambient sounds, particles, lightning shapes, sound picks, decals, the interface's
// flourishes) and whose count follows frames, the camera or what was drawn. The game drew them from g_random or
// rand(); apart, the game's draws no longer depend on them. Seeded from g_random's state when that is seeded
// (World::Create); before that it draws from the zero state, as g_random does unseeded.
extern IsaacRandom g_clientRandom;

// A draw of g_clientRandom in rand()'s range (0..0x7FFF), for the client effects that drew rand().
int ClientRand();
// ClientRand() * n / 0x7FFF % n, as libcrt::RandBelow scales rand() (n > 0).
inline int ClientRandBelow(int n) { return ClientRand() * n / 0x7FFF % n; }
