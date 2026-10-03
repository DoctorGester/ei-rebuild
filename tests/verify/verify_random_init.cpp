// Differential check for IsaacRandom::Init (random.cpp).
//
// Init seeds the generator from a random file (0049F660), so the two runs cannot draw the same
// seed on their own. The seeding call is wrapped: it records the seed material the real call
// produced, and replays it while the original Init runs on a scratch copy. Ours runs first on the
// real generator; the whole 0x814-byte state is compared.
#include "verify.h"

#include "../../src/game/random.h"

#include <cstring>

namespace {
bool s_replaySeed = false;
std::uint32_t s_seed[256];
} // namespace

// 0049F660: PUSH EBP; MOV EBP,ESP; PUSH -1; PUSH 0x716A60 (10 bytes). A pass-through that records
// the seed material (results_), replaying it while the original Init runs on its scratch copy.
static void(__fastcall* s_seedOriginal)(IsaacRandom*);
static void __fastcall SeedVerify(IsaacRandom* self) {
    if (s_replaySeed) {
        std::memcpy(self->results_, s_seed, sizeof(s_seed));
        return;
    }
    s_seedOriginal(self);
    std::memcpy(s_seed, self->results_, sizeof(s_seed));
}
EI_VERIFY(0x0049F660, SeedVerify, 10, s_seedOriginal);

// 0049F150: SUB ESP,0x20; PUSH ESI; MOV ESI,ECX (6 bytes).
static void(__fastcall* s_init)(IsaacRandom*);
static void __fastcall InitVerify(IsaacRandom* self) {
    if (self->seeded_ != 0) {
        self->IsaacRandom::Init();
        return;
    }
    alignas(4) unsigned char before[sizeof(IsaacRandom)];
    std::memcpy(before, self, sizeof(before));
    self->IsaacRandom::Init();
    alignas(4) unsigned char expected[sizeof(IsaacRandom)];
    std::memcpy(expected, before, sizeof(expected));
    s_replaySeed = true;
    s_init(reinterpret_cast<IsaacRandom*>(expected));
    s_replaySeed = false;
    ei::verify::Check("IsaacRandom::Init", true, std::memcmp(expected, self, sizeof(expected)) == 0,
                      "count %u a %08X", self->count_, self->a_);
}
EI_VERIFY(0x0049F150, InitVerify, 6, s_init);
