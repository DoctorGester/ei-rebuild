// Lifetime of the unit databases (docs/notes/units.md): the unit database (constructor, destructor) and the skill
// database. The destructors run at exit and when the database file changes (the resource reload destroys each
// table, constructs the database again and loads it).
#include "unit_db.h"

#include "lib/crt.h"

#include <cstddef>
#include <cstdlib>
#include <memory>

namespace unitdb {

UnitDatabase* ConstructUnitDatabase(UnitDatabase* self);
void DestroyUnitDatabase(UnitDatabase* self);
void DestroySkillDatabase(SkillDatabase* self);

namespace {

// A table: the name map, then the records.
template <typename Table>
void DestroyTable(Table& table) {
    std::destroy_at(&table.byName);
    std::destroy_at(&table.records);
}

// A table as the database constructor builds it: no records, an empty name map.
template <typename Table>
void ConstructTable(Table& table) {
    std::construct_at(&table.records);
    std::construct_at(&table.byName);
}

} // namespace

// The tables are destroyed last to first.
void DestroyUnitDatabase(UnitDatabase* self) {
    DestroyTable(self->levels);
    DestroyTable(self->models);
    DestroyTable(self->npcs);
    DestroyTable(self->prototypes);
}

// The skill level records, then the skills.
void DestroySkillDatabase(SkillDatabase* self) {
    std::destroy_at(&self->levels);
    std::destroy_at(&self->skills);
}

// The database constructor (the dynamic initialiser and the reload). The four tables in order: each record
// vector and name map empty. Returns the database.
UnitDatabase* ConstructUnitDatabase(UnitDatabase* self) {
    ConstructTable(self->prototypes);
    ConstructTable(self->npcs);
    ConstructTable(self->models);
    ConstructTable(self->levels);
    return self;
}

} // namespace unitdb

// ---- The unit and skill databases --------------------------------------------------------------------------------

// Built empty by static initialisers in the list's order and destroyed by the exit functions those register
// (docs/notes/standalone.md, "The static initialisers' objects").
namespace unitdb {

namespace {

void DestroyUnitDatabaseGlobal() {
    DestroyUnitDatabase(&g_unitDatabase.Get());
}
void DestroySkillDatabaseGlobal() {
    DestroySkillDatabase(&g_skillDatabase.Get());
}
} // namespace

void ConstructUnitDatabaseGlobal() {
    ConstructUnitDatabase(&g_unitDatabase.Get());
    std::atexit(DestroyUnitDatabaseGlobal);
}
void ConstructSkillDatabaseGlobal() {
    g_skillDatabase->Construct();
    std::atexit(DestroySkillDatabaseGlobal);
}

} // namespace unitdb
