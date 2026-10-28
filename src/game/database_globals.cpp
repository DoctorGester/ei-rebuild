// The resource databases that are globals of the game: the spells, the items, the quests
// the ground prints and the speeches. Each is built empty by a static initialiser in
// the list's order and destroyed by the exit function the initialiser registers with the game's atexit; both are defined
// under the names (docs/notes/standalone.md, "The static initialisers' objects"). The
// constructors (database_construct.cpp) and destructors (database_destruct.cpp, item_db_load.cpp) are the objects' own.
#include "acks_db.h"
#include "item_db.h"
#include "model.h"
#include "quest_records.h"
#include "script.h"
#include "world_data.h"

#include <cstdint>
#include <cstdlib>


constinit lib::StaticObject<itemdb::SpellDatabase> g_spellDatabase = /*007C8510*/ {};
constinit lib::StaticObject<itemdb::ItemDatabase> g_itemDatabase = /*007C86D8*/ {};
constinit lib::StaticObject<QuestDatabase> g_questDatabase = /*007C87A8*/ {};
constinit lib::StaticObject<PrintsDatabase> g_printsDatabase = /*007C89C8*/ {};
constinit lib::StaticObject<acks::AckDatabase> g_ackDatabase = /*007C8A60*/ {};

// The briefings by name (world_data.h): the database's briefing table's index.
std::unordered_map<std::string, QuestBriefingRecord*>& worlddata::QuestBriefings() { return g_questDatabase->briefings.index; }

namespace {

void DestroySpellDatabase() { g_spellDatabase->Destruct(); }
void DestroyItemDatabase() { g_itemDatabase->DestructBody(); }
void DestroyQuestDatabase() { g_questDatabase->Destruct(); }
void DestroyPrintsDatabase() { g_printsDatabase->Destruct(); }
void DestroyAckDatabase() { g_ackDatabase->Destruct(); }
} // namespace

void ConstructSpellDatabase() {
    g_spellDatabase->Construct();
    std::atexit(DestroySpellDatabase);
}

void ConstructItemDatabase() {
    g_itemDatabase->Construct();
    std::atexit(DestroyItemDatabase);
}

void ConstructQuestDatabase() {
    g_questDatabase->Construct();
    std::atexit(DestroyQuestDatabase);
}

void ConstructPrintsDatabase() {
    g_printsDatabase->Construct();
    std::atexit(DestroyPrintsDatabase);
}

void ConstructAckDatabase() {
    g_ackDatabase->Construct();
    std::atexit(DestroyAckDatabase);
}
