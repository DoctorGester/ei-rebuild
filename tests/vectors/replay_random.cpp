// Replay of the "random" family (random_vectors.h) against src/game/random.cpp: the value and the
// whole generator state afterwards, bitwise.
#include "random_vectors.h"
#include "vectors.h"

#include <cstdio>

namespace {

using namespace ei::vectors;

bool CheckNext(const random::Next& v, char* d, std::size_t n) {
    static IsaacRandom rng;   // 2 KB
    rng = v.before;
    std::uint32_t value = rng.Next();
    if (value == v.value && Same(rng, v.after)) return true;
    std::snprintf(d, n, "count %u: ours %08X, original %08X; state %s", v.before.count_, value, v.value,
                  Same(rng, v.after) ? "same" : "differs");
    return false;
}
EI_VECTOR_CHECK("IsaacRandom::Next", random::Next, CheckNext);

bool CheckRefill(const random::Refill& v, char* d, std::size_t n) {
    static IsaacRandom rng;
    rng = v.before;
    rng.Refill();
    if (Same(rng, v.after)) return true;
    std::snprintf(d, n, "a %08X b %08X c %08X: ours a %08X b %08X c %08X, original a %08X b %08X c %08X", v.before.a_,
                  v.before.b_, v.before.c_, rng.a_, rng.b_, rng.c_, v.after.a_, v.after.b_, v.after.c_);
    return false;
}
EI_VECTOR_CHECK("IsaacRandom::Refill", random::Refill, CheckRefill);

} // namespace
