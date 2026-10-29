// The unit database and the skill database: the globals and the accessors of their tables (unit.h,
// unit_create.h). Plain globals: the C++ runtime builds them empty and destroys them.
#include "unit.h"
#include "unit_db.h"


UnitDatabase g_unitDatabase;
SkillDatabase g_skillDatabase;

namespace unitdb {

// What UnitPrototypeTable::At and UnitModelTable::At answer (the vector replay links this file alone).
UnitPrototypeRecord* PrototypeRecord(std::uint32_t index) { return RecordAt(g_unitDatabase.prototypes.records, index); }
UnitModelRecord* ModelRecord(std::uint32_t index) { return RecordAt(g_unitDatabase.models.records, index); }

} // namespace unitdb
