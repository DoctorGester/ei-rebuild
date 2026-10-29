// The digest's reader of game.exe's objects (harness_digest_read.h), for game.exe's runs: the reading code over the
// views of exe_objects.h, at game.exe's offsets. No game header: our classes do not decide what the original records.
#include "harness_digest_read.h"
#include "harness_heap.h"
#include "harness_original.h"
#include "exe_objects.h"

namespace ei::verify::digest::exe_side {

using namespace ei::verify::exe;

namespace {

// The generators: game.exe's ISAAC (007A9D40) and its CRT rand() seed (_getptd's +0x14).
void ReadRandom(Builder& b) {
    const IsaacRandom& r = Random();
    b.Unsigned("isaac.seeded", r.seeded);
    b.Unsigned("isaac.count", r.count);
    b.Bits("isaac.a", r.aa);
    b.Bits("isaac.b", r.bb);
    b.Bits("isaac.c", r.cc);
    b.Bits("isaac.results", HashWords(r.results, 256));
    b.Bits("isaac.memory", HashWords(r.memory, 256));
    b.Bits("crt.rand", OriginalRandState());
}

// An AI mode's id: its vtable (game.exe's, which the kinds are named by), 0 for none.
std::uint32_t ModeId(AIMode* mode) {
    return mode ? *reinterpret_cast<const std::uint32_t*>(mode) : 0;
}

int PartyItemCount(CParty* party) { return party->items.size; }

std::uint32_t MissileVtable(const CSpellEffect* e) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(e->vtable));
}

// A cast spell's missile has CSpellEffect's table (0074399C).
bool IsSpellEffect(const CSpellEffect* e) {
    return e->vtable == &OriginalGameData<const void* const>(0x0074399C);
}

} // namespace

#include "harness_digest_read.inc"

} // namespace ei::verify::digest::exe_side
