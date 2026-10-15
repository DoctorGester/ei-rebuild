// Record mode for the "random" family (random_vectors.h). Stolen bytes as in verify_maths.cpp. The
// generator is drawn from by AI, combat and loot (units, sandbox_actions); Refill runs every 256 draws.
#include "random_vectors.h"
#include "vectors.h"

#include "ei/hook.h"

namespace {

using namespace ei::vectors;

// The payloads are 4 KB and never repeat: no repeat check, a few per path.
Sampler g_next("random", "IsaacRandom::Next", sizeof(random::Next), 12, 2);
Sampler g_refill("random", "IsaacRandom::Refill", sizeof(random::Refill), 8);

// 004667F0. Bucket 1: the call that refilled (count_ was 0).
std::uint32_t(__fastcall* s_next)(IsaacRandom*);
std::uint32_t __fastcall NextRecord(IsaacRandom* rng) {
    random::Next v;   // no padding: every byte is written
    std::memcpy(&v.before, rng, sizeof(IsaacRandom));
    std::uint16_t cw = ControlWord();
    std::uint32_t value = s_next(rng);
    std::memcpy(&v.after, rng, sizeof(IsaacRandom));
    v.value = value;
    g_next.Offer(v, cw, v.before.count_ == 0 ? 1 : 0, false);
    return value;
}

// 0049EE70
void(__fastcall* s_refill)(IsaacRandom*);
void __fastcall RefillRecord(IsaacRandom* rng) {
    random::Refill v;
    std::memcpy(&v.before, rng, sizeof(IsaacRandom));
    std::uint16_t cw = ControlWord();
    s_refill(rng);
    std::memcpy(&v.after, rng, sizeof(IsaacRandom));
    g_refill.Offer(v, cw, 0, false);
}

} // namespace

EI_RECORD(0x004667F0, NextRecord, 6, s_next);
EI_RECORD(0x0049EE70, RefillRecord, 6, s_refill);
