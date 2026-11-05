// The resource databases that are globals of the game: the spells, the items, the quests,
// the ground prints and the speeches. Each class constructs and destroys its members itself; the C++ runtime
// builds the globals empty and destroys them.
#include "game/acks_db.h"
#include "game/item_db.h"
#include "game/model.h"
#include "game/quest_records.h"


itemdb::SpellDatabase g_spellDatabase;
itemdb::ItemDatabase g_itemDatabase;
QuestDatabase g_questDatabase;
PrintsDatabase g_printsDatabase;
acks::AckDatabase g_ackDatabase;
