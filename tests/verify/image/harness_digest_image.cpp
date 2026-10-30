// The digest's reader of the image's objects (harness_digest_read.h): the reading code over our classes. The image's
// side of the harness only (tests/verify/image): run_all's reference key leaves it out.
#include "../harness_digest_read.h"
#include "../harness_heap.h"

#include "game/ai_motion.h"
#include "game/combat.h"
#include "game/game_singletons.h"
#include "game/map_object.h"
#include "game/party.h"
#include "game/player_manager.h"
#include "game/player_messages.h"
#include "game/random.h"
#include "game/script.h"
#include "game/spell.h"
#include "game/terrain.h"
#include "game/terrain_sector.h"
#include "game/unit.h"
#include "game/unit_ai.h"
#include "game/world.h"
#include "game/world_effect.h"
#include "game/asset_text.h"
#include "lib/crt.h"

#include <typeinfo>

namespace ei::verify::digest::image_side {

namespace {

WorldClient* ClientWorld() { return &g_clientWorld; }

// The generators: ours (named globals, C18l).
void ReadRandom(Builder& b) {
    const IsaacRandom& r = g_random;
    b.Unsigned("isaac.seeded", r.seeded);
    b.Unsigned("isaac.count", r.count);
    b.Bits("isaac.a", r.aa);
    b.Bits("isaac.b", r.bb);
    b.Bits("isaac.c", r.cc);
    b.Bits("isaac.results", HashWords(r.results, 256));
    b.Bits("isaac.memory", HashWords(r.memory, 256));
    b.Bits("crt.rand", libcrt::RandState());
}

int PartyItemCount(CParty* party) { return static_cast<int>(party->items.size()); }

// An AI mode's id: its kind's (ModeKindId, by GetType: the kinds' order with Follow's 10 last), else its table's
// address (none: 0).
std::uint32_t ModeId(AIMode* mode) {
    if (!mode) return 0;
    const AIModeType type = mode->type;
    if (type >= AIModeType::Guard && type <= AIModeType::Tamed) return ModeKindId(static_cast<std::size_t>(type));
    if (type == AIModeType::Follow) return ModeKindId(9);
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(*reinterpret_cast<const void* const*>(mode)));
}

std::uint32_t ModeId(const std::unique_ptr<AIMode>& mode) { return ModeId(mode.get()); }

std::uint32_t MissileVtable(const SpellEffect* e) {
    std::uint32_t vtable;
    std::memcpy(&vtable, static_cast<const void*>(e), 4);
    return vtable;
}

bool IsSpellEffect(const SpellEffect* e) { return typeid(*e) == typeid(SpellEffect); }

// A script effect's object id (-1: none); the reader's IdOf takes the object pointers.
template <class T>
int IdOf(const ScriptEffect<T>& effect) {
    return effect.SavedId();
}

// Our text, UTF-8, in the asset code page game.exe's text is in, so both sides digest the same bytes. Our std::string
// members as game.exe's CStrings (read as const char*).
std::string Text(const char* s) { return ::text::ToAssetText(digest::Text(s)); }
std::string Text(const std::string& s) { return ::text::ToAssetText(s.c_str()); }

} // namespace

#include "../harness_digest_read.inc"

} // namespace ei::verify::digest::image_side
