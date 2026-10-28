// The compiler-generated destructors of the global databases. Only the exit functions call them
// (src/standalone/static_exit.cpp), so they run at a normal exit in the hook build too. Each destroys the
// members from the last: a table's name index, then its records. The constructors are database_construct.cpp's.
#include "acks_db.h"
#include "item_db.h"
#include "model.h"
#include "quest_records.h"

#include <memory>
#include <vector>

namespace {

// A table of records and its index (the index declared after the records): the index, then the records.
template <typename Record, typename Index>
void DestroyTable(std::vector<Record>& records, Index& index) {
    std::destroy_at(&index);
    std::destroy_at(&records);
}

} // namespace

namespace itemdb {

void SpellDatabase::Destruct() {
    for (int i = 2; i >= 0; --i) std::destroy_at(&lists[i]);
    modifiers.Destruct();
    spells.Destruct();
}

} // namespace itemdb

void QuestDatabase::Destruct() {
    DestroyTable(briefings.records, briefings.index);
    DestroyTable(quests, questsByName);
}

void PrintsDatabase::Destruct() {
    DestroyTable(scorch, scorchByName);
    DestroyTable(footprints, footprintsByName);
    DestroyTable(blood, bloodByName);
}

namespace acks {

void AckDatabase::Destruct() {
    DestroyTable(extras.records, extras.byName);
    DestroyTable(models.records, models.byName);
    DestroyTable(units.records, units.byName);
}

} // namespace acks
