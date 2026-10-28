// The compiler-generated destructors of the global databases. Only the exit functions call them
// (src/standalone/static_exit.cpp), so they run at a normal exit in the hook build too. Each destroys the
// members from the last: a table's name index, then its records through their vector's element policy
// (record_vectors.h: each record in order, then the storage). The constructors are database_construct.cpp's.
#include "acks_db.h"
#include "game_arrays.h"
#include "game_slots.h"
#include "item_db.h"
#include "model.h"
#include "object_tables.h"
#include "quest_records.h"
#include "record_lifetime.h"
#include "record_vectors.h"

#include "lib/mfc_slots.h"
#include "game_data.h"

#include <memory>

namespace {

// A table of records and its index (the index declared after the records): the index, then the
// records as `Abi` destroys them.
template <typename Abi, typename Index>
void DestroyTable(typename Abi::Vector& records, Index& index) {
    std::destroy_at(&index);
    Abi::Destruct(&records);
}

} // namespace

void CMapDatabase::Destruct() {
    maps.vtable = gameslots::MapRecordArrayClass::Vtable();
    maps.FreeData();
    allods.vtable = mfc::impl::AllodArrayClass::Vtable();
    allods.FreeData();
}

namespace itemdb {

void SpellDatabase::Destruct() {
    for (int i = 2; i >= 0; --i) recordvec::SpellListVector::Destruct(&lists[i]);
    DestroyTable<recordvec::SpellModifierVector>(modifiers.records, modifiers.byCode);
    DestroyTable<recordvec::SpellVector>(spells.records, spells.byCode);
}

} // namespace itemdb

void QuestDatabase::Destruct() {
    DestroyTable<recordvec::BriefingVector>(briefings.records, briefings.index);
    DestroyTable<recordvec::QuestVector>(quests, questsByName);
}

void PrintsDatabase::Destruct() {
    DestroyTable<recordvec::PrintVector>(scorch, scorchByName);
    DestroyTable<recordvec::Print2Vector>(footprints, footprintsByName);
    DestroyTable<recordvec::PrintVector>(blood, bloodByName);
}

namespace acks {

void AckDatabase::Destruct() {
    DestroyTable<recordvec::ExtraAcksVector>(extras.records, extras.byName);
    DestroyTable<recordvec::ModelAcksVector>(models.records, models.byName);
    DestroyTable<recordvec::UnitAcksVector>(units.records, units.byName);
}

} // namespace acks
