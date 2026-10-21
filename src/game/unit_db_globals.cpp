// The unit database (007AF358) and the skill database (007AF268): the globals and the views of their tables
// (unit.h, unit_create.h). Built empty by static initialisers and destroyed by exit functions that unit_db_destroy.cpp
// ports on these names (docs/notes/standalone.md, "The static initialisers' objects"). Apart from the destructors so
// that the vector replay (ei_vectors) links it.
#include "item_db.h"   // RecordTable
#include "unit.h"
#include "unit_db.h"

static_assert(sizeof(UnitDatabase) == 0x80 && sizeof(SkillDatabase) == 0x18);

constinit lib::StaticObject<UnitDatabase> g_unitDatabase = /*007AF358*/ {};
constinit lib::StaticObject<SkillDatabase> g_skillDatabase = /*007AF268*/ {};

namespace unitdb {

// The tables as the record-table views read them (begin / end of the records first).
UnitPrototypeTable& PrototypeTable() { return g_unitDatabase->prototypes; }
NpcPrototypeTable& NpcTable() { return g_unitDatabase->npcs; }
const RecordTable& Prototypes() { return reinterpret_cast<const RecordTable&>(g_unitDatabase->prototypes); }
const RecordTable& Models() { return reinterpret_cast<const RecordTable&>(g_unitDatabase->models); }
const RecordTable& ModelLevels() { return reinterpret_cast<const RecordTable&>(g_unitDatabase->levels); }
const RecordTable& Skills() { return reinterpret_cast<const RecordTable&>(g_skillDatabase->skills); }
const RecordTable& SkillLevels() { return reinterpret_cast<const RecordTable&>(g_skillDatabase->levels); }

} // namespace unitdb
