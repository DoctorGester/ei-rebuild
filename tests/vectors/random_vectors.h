#pragma once

// Payloads of the "random" family (vectors.h): the game's ISAAC generator (src/game/random.cpp).
// Recorded by record_random.cpp, replayed by replay_random.cpp.

#include "../../src/game/random.h"

#include <cstdint>

namespace ei::vectors::random {

// IsaacRandom::Next. Bucket 1: the call that ran out of results and refilled.
struct Next {
    IsaacRandom before, after;
    std::uint32_t value;
};
// IsaacRandom::Refill.
struct Refill {
    IsaacRandom before, after;
};

static_assert(sizeof(Next) == 2 * 0x814 + 4 && sizeof(Refill) == 2 * 0x814);

} // namespace ei::vectors::random
