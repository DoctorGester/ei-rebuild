// The unit database and the skill database: the globals and the accessors of their tables (unit.h,
// unit_create.h). Built empty by static initialisers and destroyed by exit functions that
// unit_db_destroy.cpp defines on these names (docs/notes/standalone.md, "The static initialisers'
// objects"). Apart from the destructors so that the vector replay (ei_vectors) links it.
#include "unit.h"
#include "unit_db.h"


constinit lib::StaticObject<UnitDatabase> g_unitDatabase = /*007AF358*/ {};
constinit lib::StaticObject<SkillDatabase> g_skillDatabase = /*007AF268*/ {};

namespace unitdb {

UnitPrototypeTable& PrototypeTable() { return g_unitDatabase->prototypes; }
NpcPrototypeTable& NpcTable() { return g_unitDatabase->npcs; }
namespace {
// Element `index` of the records between `begin` and `end`, or null past the end (what
// UnitPrototypeTable::At and UnitModelTable::At answer, inline: the vector replay links this file alone).
template <typename T>
T* RecordAt(T* begin, T* end, std::uint32_t index) {
    return index < static_cast<std::uint32_t>(end - begin) ? begin + index : nullptr;
}
} // namespace

UnitPrototypeRecord* PrototypeRecord(std::uint32_t index) {
    return RecordAt(g_unitDatabase->prototypes.begin, g_unitDatabase->prototypes.end, index);
}
UnitModelRecord* ModelRecord(std::uint32_t index) { return RecordAt(g_unitDatabase->models.begin, g_unitDatabase->models.end, index); }

} // namespace unitdb
