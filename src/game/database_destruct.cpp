// The compiler-generated destructors of the global databases (S3c). Only the exit functions call
// them (src/standalone/static_exit.cpp: 0045E6E0, 00684380, 006ADF30, 006D00D0, 006D70A0), so
// they run at a normal exit in the hook build too. Each destroys the members from the last: a
// table's name index (hash_map::Destroy: every chain from its head, each node's key string then
// the node, then the buckets), then its records through their vector's element policy
// (record_vectors.h: each record in order, then the storage). The constructors are
// database_construct.cpp's. Checked in game by verify_exit_destructors.cpp until S21d.
#include "acks_db.h"
#include "game_arrays.h"
#include "item_db.h"
#include "model.h"
#include "object_tables.h"
#include "quest_records.h"
#include "record_lifetime.h"
#include "record_vectors.h"

#include "ei/hook.h"
#include "lib/mfc_slots.h"
#include "game_data.h"

namespace {

// A table of records and its index (the index declared after the records): the index, then the
// records as `Abi` destroys them.
template <typename Abi, typename Index>
void DestroyTable(typename Abi::Vector& records, Index& index) {
    index.Destroy();
    Abi::Destruct(&records);
}

// An allod: its map indices' array (the CDWordArray's vtable back, the storage freed), then its
// strings from the last.
struct AllodElements {
    static void Construct(AllodRecord*, int) {}
    static void Destruct(AllodRecord* allod, int count) {
        for (; count > 0; --count, ++allod) {
            allod->maps.vtable = mfc::impl::IntArrayClass::Vtable();
            allod->maps.FreeData();
            for (int i = 4; i >= 0; --i) allod->otherNames[i].~CString();
            allod->name.~CString();
        }
    }
};

} // namespace

void CMapDatabase::Destruct() {
    auto& mapRecords = reinterpret_cast<gamearrays::ZoneRecords::Array&>(maps);
    mapRecords.vtable = ei::GamePointer<void>(kMapArrayVtable);
    mapRecords.FreeData();
    auto& allodRecords = reinterpret_cast<mfc::CArray<AllodRecord, AllodElements>&>(allods);
    allodRecords.vtable = mfc::impl::AllodArrayClass::Vtable();
    allodRecords.FreeData();
}
EI_REPLACE_METHOD(0x0045E6F0, CMapDatabase::Destruct);

namespace itemdb {

void SpellDatabase::Destruct() {
    for (int i = 2; i >= 0; --i) recordvec::SpellListVector::Destruct(&lists[i]);
    // A table's first three words are its records' vector.
    DestroyTable<recordvec::SpellModifierVector>(reinterpret_cast<recordvec::SpellModifierVector::Vector&>(modifiers),
                                                 modifiers.byCode);
    DestroyTable<recordvec::SpellVector>(reinterpret_cast<recordvec::SpellVector::Vector&>(spells), spells.byCode);
}

} // namespace itemdb

EI_REPLACE_METHOD(0x00684390, itemdb::SpellDatabase::Destruct);

void QuestDatabase::Destruct() {
    DestroyTable<recordvec::BriefingVector>(briefings.records, briefings.index);
    DestroyTable<recordvec::QuestVector>(quests, questsByName);
}
EI_REPLACE_METHOD(0x006ADF40, QuestDatabase::Destruct);

void PrintsDatabase::Destruct() {
    DestroyTable<recordvec::PrintVector>(scorch, scorchByName);
    DestroyTable<recordvec::Print2Vector>(footprints, footprintsByName);
    DestroyTable<recordvec::PrintVector>(blood, bloodByName);
}
EI_REPLACE_METHOD(0x006D00E0, PrintsDatabase::Destruct);

namespace acks {

void AckDatabase::Destruct() {
    DestroyTable<recordvec::ExtraAcksVector>(extras.records, extras.byName);
    DestroyTable<recordvec::ModelAcksVector>(models.records, models.byName);
    DestroyTable<recordvec::UnitAcksVector>(units.records, units.byName);
}

} // namespace acks

EI_REPLACE_METHOD(0x006D70B0, acks::AckDatabase::Destruct);
