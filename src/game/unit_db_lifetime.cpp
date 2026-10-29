// The unit and skill databases' static initialisers and exit functions (docs/notes/standalone.md, "The static
// initialisers' objects"): each database is built empty in the list's order and destroyed at exit. Apart from
// unit_db_globals.cpp so that the vector replay (ei_vectors) links that file without the records' destructors.
#include "unit_db.h"

#include <cstdlib>

namespace unitdb {

namespace {

void DestroyUnitDatabaseGlobal() { g_unitDatabase.Destroy(); }
void DestroySkillDatabaseGlobal() { g_skillDatabase.Destroy(); }

} // namespace

void ConstructUnitDatabaseGlobal() {
    g_unitDatabase.Construct();
    std::atexit(DestroyUnitDatabaseGlobal);
}
void ConstructSkillDatabaseGlobal() {
    g_skillDatabase.Construct();
    std::atexit(DestroySkillDatabaseGlobal);
}

} // namespace unitdb
