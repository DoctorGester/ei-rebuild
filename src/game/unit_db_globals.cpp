// The unit database and the skill database: the globals and the accessors of their tables (unit.h,
// unit_create.h). Built empty by static initialisers and destroyed by exit functions that
// unit_db_lifetime.cpp defines on these names (docs/notes/standalone.md, "The static initialisers'
// objects").
#include "unit.h"
#include "unit_db.h"


constinit lib::StaticObject<UnitDatabase> g_unitDatabase = /*007AF358*/ {};
constinit lib::StaticObject<SkillDatabase> g_skillDatabase = /*007AF268*/ {};

namespace unitdb {

// What UnitPrototypeTable::At and UnitModelTable::At answer (the vector replay links this file alone).
UnitPrototypeRecord* PrototypeRecord(std::uint32_t index) { return RecordAt(g_unitDatabase->prototypes.records, index); }
UnitModelRecord* ModelRecord(std::uint32_t index) { return RecordAt(g_unitDatabase->models.records, index); }

} // namespace unitdb
