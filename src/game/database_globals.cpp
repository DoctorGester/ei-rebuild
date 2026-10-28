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

static_assert(sizeof(itemdb::SpellDatabase) == 0x64 && sizeof(itemdb::ItemDatabase) == 0xC0);
static_assert(sizeof(QuestDatabase) == 0x40 && sizeof(PrintsDatabase) == 0x60 && sizeof(acks::AckDatabase) == 0x60);

constinit lib::StaticObject<itemdb::SpellDatabase> g_spellDatabase = /*007C8510*/ {};
constinit lib::StaticObject<itemdb::ItemDatabase> g_itemDatabase = /*007C86D8*/ {};
constinit lib::StaticObject<QuestDatabase> g_questDatabase = /*007C87A8*/ {};
constinit lib::StaticObject<PrintsDatabase> g_printsDatabase = /*007C89C8*/ {};
constinit lib::StaticObject<acks::AckDatabase> g_ackDatabase = /*007C8A60*/ {};

// The briefings by name (world_data.h): the database's briefing table's index.
QuestBriefingTable::Index& worlddata::QuestBriefings() { return TheQuestDatabase().briefings.index; }

namespace {

using AtExitFunction = void(__cdecl*)();
void RegisterExit(AtExitFunction exitFunction) { libcrt::AtExit(exitFunction); }

void DestroySpellDatabase() { g_spellDatabase->Destruct(); }
void DestroyItemDatabase() { g_itemDatabase->DestructBody(); }
void DestroyQuestDatabase() { g_questDatabase->Destruct(); }
void DestroyPrintsDatabase() { g_printsDatabase->Destruct(); }
void DestroyAckDatabase() { g_ackDatabase->Destruct(); }
} // namespace

void ConstructSpellDatabase() {
    g_spellDatabase->Construct();
    RegisterExit(DestroySpellDatabase);
}

void ConstructItemDatabase() {
    g_itemDatabase->Construct();
    RegisterExit(DestroyItemDatabase);
}

void ConstructQuestDatabase() {
    g_questDatabase->Construct();
    RegisterExit(DestroyQuestDatabase);
}

void ConstructPrintsDatabase() {
    g_printsDatabase->Construct();
    RegisterExit(DestroyPrintsDatabase);
}

void ConstructAckDatabase() {
    g_ackDatabase->Construct();
    RegisterExit(DestroyAckDatabase);
}
