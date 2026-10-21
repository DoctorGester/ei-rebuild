// The resource databases that are globals of the game: the spells (007C8510), the items (007C86D8), the quests
// (007C87A8), the ground prints (007C89C8) and the speeches (007C8A60). Each is built empty by a static initialiser in
// the list's order and destroyed by the exit function the initialiser registers with the game's atexit; both are ported
// on the names and registered at their addresses (docs/notes/standalone.md, "The static initialisers' objects"). The
// constructors (database_construct.cpp) and destructors (database_destruct.cpp, item_db_load.cpp) are the objects' own.
#include "acks_db.h"
#include "item_db.h"
#include "model.h"
#include "quest_records.h"
#include "script.h"
#include "world_data.h"

#include "ei/hook.h"
#include "originals_library.h"

#include <cstdint>

static_assert(sizeof(itemdb::SpellDatabase) == 0x64 && sizeof(itemdb::ItemDatabase) == 0xC0);
static_assert(sizeof(QuestDatabase) == 0x40 && sizeof(PrintsDatabase) == 0x60 && sizeof(acks::AckDatabase) == 0x60);

constinit lib::StaticObject<itemdb::SpellDatabase> g_spellDatabase = /*007C8510*/ {};
constinit lib::StaticObject<itemdb::ItemDatabase> g_itemDatabase = /*007C86D8*/ {};
constinit lib::StaticObject<QuestDatabase> g_questDatabase = /*007C87A8*/ {};
constinit lib::StaticObject<PrintsDatabase> g_printsDatabase = /*007C89C8*/ {};
constinit lib::StaticObject<acks::AckDatabase> g_ackDatabase = /*007C8A60*/ {};

// The briefings by name (world_data.h): the database's briefing table's index.
stlp::NameMap& worlddata::QuestBriefings() { return reinterpret_cast<stlp::NameMap&>(TheQuestDatabase().briefings.index); }
// The quests as the script commands read them (script.h): the database's first half.
QuestTable* TheQuestTable() { return reinterpret_cast<QuestTable*>(&TheQuestDatabase()); }

namespace {

using AtExitFunction = void(__cdecl*)();
void RegisterExit(std::uintptr_t exitFunction) { GameAtExit(ei::Code<AtExitFunction>(exitFunction)); }

void __cdecl ConstructSpellDatabase() {   // 00684360
    g_spellDatabase->Construct();
    RegisterExit(0x00684380);
}
void __cdecl DestroySpellDatabase() { g_spellDatabase->Destruct(); }   // 00684380

void __cdecl ConstructItemDatabase() {   // 006A6540
    g_itemDatabase->Construct();
    RegisterExit(0x006A6560);
}
void __cdecl DestroyItemDatabase() { g_itemDatabase->DestructBody(); }   // 006A6560

void __cdecl ConstructQuestDatabase() {   // 006ADF10
    g_questDatabase->Construct();
    RegisterExit(0x006ADF30);
}
void __cdecl DestroyQuestDatabase() { g_questDatabase->Destruct(); }   // 006ADF30

void __cdecl ConstructPrintsDatabase() {   // 006D00B0
    g_printsDatabase->Construct();
    RegisterExit(0x006D00D0);
}
void __cdecl DestroyPrintsDatabase() { g_printsDatabase->Destruct(); }   // 006D00D0

void __cdecl ConstructAckDatabase() {   // 006D7080
    g_ackDatabase->Construct();
    RegisterExit(0x006D70A0);
}
void __cdecl DestroyAckDatabase() { g_ackDatabase->Destruct(); }   // 006D70A0

} // namespace

EI_REPLACE(0x00684360, ConstructSpellDatabase);
EI_REPLACE(0x00684380, DestroySpellDatabase);
EI_REPLACE(0x006A6540, ConstructItemDatabase);
EI_REPLACE(0x006A6560, DestroyItemDatabase);
EI_REPLACE(0x006ADF10, ConstructQuestDatabase);
EI_REPLACE(0x006ADF30, DestroyQuestDatabase);
EI_REPLACE(0x006D00B0, ConstructPrintsDatabase);
EI_REPLACE(0x006D00D0, DestroyPrintsDatabase);
EI_REPLACE(0x006D7080, ConstructAckDatabase);
EI_REPLACE(0x006D70A0, DestroyAckDatabase);
