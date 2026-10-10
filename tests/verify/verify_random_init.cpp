// Checks for the ISAAC generator's seeding (random.cpp), reached once per run from the world's
// set-up 0055EE40 (newgame and every scenario that makes a world). In the harness's deterministic mode
// 0049F660 is replaced by a fixed seed, so only Init's check runs there.
//
// - Init (0049F150): differential. Init seeds the generator from a random file (0049F660), so the
//   two runs cannot draw the same seed on their own. The seeding call is wrapped: it records the
//   seed material the real call produced, and replays it while the original Init runs on a scratch
//   copy. Ours runs first on the real generator; the whole 0x814-byte state is compared.
// - Seed (0049F660): ours only (time and rand() based): results_ ends up not all zero and the other
//   fields are untouched.
// - FindSeedFile (0049F4B0): against the original. The original walks first into a scratch buffer
//   and counter (its recursive calls come back here and run the original only), then ours on the
//   real ones; the result, the counter and, when found, the path are compared. Only a change on
//   drive C between the two walks can make them differ.
#include "verify.h"

#include "../../src/game/random.h"

#include <cstring>

namespace {
bool s_replaySeed = false;
std::uint32_t s_seed[256];
} // namespace

// 0049F660: PUSH EBP; MOV EBP,ESP; PUSH -1; PUSH 0x716A60 (10 bytes). Runs ours and records the
// seed material (results_), replaying it while the original Init runs on its scratch copy.
static void(__fastcall* s_seedOriginal)(IsaacRandom*);
static void __fastcall SeedVerify(IsaacRandom* self) {
    if (s_replaySeed) {
        std::memcpy(self->results_, s_seed, sizeof(s_seed));
        return;
    }
    std::uint32_t seeded = self->seeded_;
    std::uint32_t count = self->count_;
    self->IsaacRandom::Seed();
    std::memcpy(s_seed, self->results_, sizeof(s_seed));
    bool nonzero = false;
    for (std::uint32_t word : self->results_) nonzero = nonzero || word != 0;
    ei::verify::Check("IsaacRandom::Seed", true, nonzero && self->seeded_ == seeded && self->count_ == count,
                      "results %s", nonzero ? "set" : "all zero");
}
EI_VERIFY(0x0049F660, SeedVerify, 10, s_seedOriginal);

// 0049F4B0: PUSH -1 (2), PUSH imm32 (5).
using FindFn = int(__fastcall*)(IsaacRandom*, void*, char*, const char*, int, int*);
static FindFn s_findSeedFile;
static bool s_inOriginalFind = false;
static int __fastcall FindSeedFileVerify(IsaacRandom* self, void*, char* out, const char* pattern, int target,
                                         int* counter) {
    if (s_inOriginalFind) return s_findSeedFile(self, nullptr, out, pattern, target, counter);
    char expectedPath[0x11C] = {};
    int expectedCounter = *counter;
    s_inOriginalFind = true;
    int expected = s_findSeedFile(self, nullptr, expectedPath, pattern, target, &expectedCounter);
    s_inOriginalFind = false;
    int actual = self->IsaacRandom::FindSeedFile(out, pattern, target, counter);
    bool ok = expected == actual && expectedCounter == *counter && (actual != 1 || std::strcmp(expectedPath, out) == 0);
    ei::verify::Check("IsaacRandom::FindSeedFile", true, ok, "target %d: %d / %d, counter %d / %d", target, expected,
                      actual, expectedCounter, *counter);
    return actual;
}
EI_VERIFY(0x0049F4B0, FindSeedFileVerify, 7, s_findSeedFile);

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
