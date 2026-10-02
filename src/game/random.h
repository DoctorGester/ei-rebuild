#pragma once

#include <cstdint>

// The game's random number generator: Bob Jenkins' ISAAC (RANDSIZL = 8), the reference
// implementation's randctx with one extra leading field. Used for AI, combat and loot rolls, so it
// must stay bit-exact. 0x814 bytes.
class IsaacRandom {
public:
    // 004667F0. Next 32-bit random value.
    std::uint32_t Next();

    // 0049EE70. Generates the next 256 results (reference isaac()).
    void Refill();

    // 0049F150 initialises the state (reference randinit()); not rewritten yet.

    std::uint32_t seeded_;          // +0x000 non-zero once seeded from results_
    std::uint32_t count_;           // +0x004 results left (randcnt)
    std::uint32_t results_[256];    // +0x008 randrsl
    std::uint32_t memory_[256];     // +0x408 randmem
    std::uint32_t a_, b_, c_;       // +0x808 randa, randb, randc
};
static_assert(sizeof(IsaacRandom) == 0x814);
