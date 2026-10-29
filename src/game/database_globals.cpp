// The resource databases that are globals of the game: the spells, the items, the quests,
// the ground prints and the speeches. Each class constructs and destroys its members itself. The ones the
// interface's files spell with `->` stay lib::StaticObject, built empty by a static initialiser in the list's order
// and destroyed by the exit function it registers (docs/notes/standalone.md, "The static initialisers' objects");
// the prints database is a plain global.
#include "acks_db.h"
#include "item_db.h"
#include "model.h"
#include "quest_records.h"
#include "world_data.h"

#include <cstdlib>


constinit lib::StaticObject<itemdb::SpellDatabase> g_spellDatabase = /*007C8510*/ {};
constinit lib::StaticObject<itemdb::ItemDatabase> g_itemDatabase = /*007C86D8*/ {};
constinit lib::StaticObject<QuestDatabase> g_questDatabase = /*007C87A8*/ {};
PrintsDatabase g_printsDatabase;
constinit lib::StaticObject<acks::AckDatabase> g_ackDatabase = /*007C8A60*/ {};

// The briefings by name (world_data.h): the database's briefing table's index.
std::unordered_map<std::string, QuestBriefingRecord*>& worlddata::QuestBriefings() { return g_questDatabase->briefings.index; }

namespace {

void DestroySpellDatabase() { g_spellDatabase.Destroy(); }
void DestroyItemDatabase() { g_itemDatabase.Destroy(); }
void DestroyQuestDatabase() { g_questDatabase.Destroy(); }
void DestroyAckDatabase() { g_ackDatabase.Destroy(); }
} // namespace

void ConstructSpellDatabase() {
    g_spellDatabase.Construct();
    std::atexit(DestroySpellDatabase);
}

void ConstructItemDatabase() {
    g_itemDatabase.Construct();
    std::atexit(DestroyItemDatabase);
}

void ConstructQuestDatabase() {
    g_questDatabase.Construct();
    std::atexit(DestroyQuestDatabase);
}

void ConstructAckDatabase() {
    g_ackDatabase.Construct();
    std::atexit(DestroyAckDatabase);
}
