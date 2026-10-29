// The resource databases that are globals of the game: the spells, the items, the quests,
// the ground prints and the speeches. Each class constructs and destroys its members itself; the C++ runtime
// builds the globals empty and destroys them.
#include "acks_db.h"
#include "item_db.h"
#include "model.h"
#include "quest_records.h"
#include "world_data.h"


itemdb::SpellDatabase g_spellDatabase;
itemdb::ItemDatabase g_itemDatabase;
QuestDatabase g_questDatabase;
PrintsDatabase g_printsDatabase;
acks::AckDatabase g_ackDatabase;

// The briefings by name (world_data.h): the database's briefing table's index.
std::unordered_map<std::string, QuestBriefingRecord*>& worlddata::QuestBriefings() { return g_questDatabase.briefings.index; }
