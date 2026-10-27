// The item database's name-indexed tables read from an archive stream: quick items, quest items and loot. See
// item_db.h (itemdb::NamedRecordTable) and docs/notes/items_spells.md ("Databases and naming").
#include "item_db.h"

#include "chunk_archive.h"
#include "core_streams.h"
#include "record_vectors.h"

#include "ei/hook.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "lib/stlp_vector.h"

#include <functional>
#include <type_traits>

namespace {
// An item record's destructor (the kinds' are not reachable as member pointers), for the tables'
// loaders and destructor.
template <typename Record>
void DestroyItemRecord(Record* record) { record->~Record(); }
} // namespace

namespace {

// The records read through an archive, then each filed in the name index under its name (the index's
// find_or_insert of {name, null}). The chunk serialisers' names cross over: SerializeLoot reads the quest
// items, SerializeQuestItems the loot.
template <typename Record>
void LoadIndexed(itemdb::NamedRecordTable<Record>* table, CStream* source, int field) {
    ChunkArchive archive;
    archive.Open(source, 1, 1);
    if constexpr (std::is_same_v<Record, itemdb::QuickItemRecord>)
        archive.SerializeQuickItems(field, &table->records, nullptr);
    else if constexpr (std::is_same_v<Record, itemdb::QuestItemRecord>)
        archive.SerializeLoot(field, &table->records, nullptr);
    else
        archive.SerializeQuestItems(field, &table->records, nullptr);
    archive.Close(source);
    for (Record* record = table->records.begin(); record != table->records.end(); ++record) {
        stlp::string name(record->text04);
        stlp::NamedSlot<Record>(table->names, name) = record;
    }
}

} // namespace

namespace itemdb {

template <typename Record>
void NamedRecordTable<Record>::Load(CStream* source, int field) {
    LoadIndexed(this, source, field);
}
template void QuickItemTable::Load(CStream*, int);
template void QuestItemTable::Load(CStream*, int);
template void LootItemTable::Load(CStream*, int);

} // namespace itemdb

// --- The tables' vector serialisers ----------------------------------------------------------

namespace {

using ResizeFn = void (*)(void* records, unsigned count);

// Per record type: the record's destructor (plain RET), the vector's resize (RET 4: new records
// default-constructed) and the record's Serialize (RET 4).

// The body of the three instantiations: chunk `id` holding one chunk 1 per record; loading destroys the old
// records (erase(begin, end): the tail copy is empty) and resizes to the number of record chunks first.
template <typename Record, typename Destroy, typename Serialize>
void SerializeItemRecords(ChunkArchive* ar, int id, void* vector, Destroy destroy, ResizeFn resize, Serialize serialize) {
    if (!ar->EnterChunk(id)) return;
    auto& records = *static_cast<stlp::vector<Record>*>(vector);
    int count;
    if (ar->loading_) {
        Record* end = records.end();
        for (Record* p = records.begin(); p != end; ++p) std::invoke(destroy, p);
        records.Finish() = records.begin();
        count = ar->CountChunks(1);
        resize(vector, static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(records.size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        Record* record = records.begin() + i;
        if (ar->EnterChunk(1)) {
            (record->*serialize)(ar);
            ar->LeaveChunk();
        }
    }
    ar->LeaveChunk();
}

} // namespace

void ChunkArchive::SerializeQuickItems(int id, void* records, void*) {
    SerializeItemRecords<itemdb::QuickItemRecord>(this, id, records,
                                    &DestroyItemRecord<itemdb::QuickItemRecord>, recordvec::ResizeUntyped<recordvec::QuickItemVector>, &itemdb::QuickItemRecord::Serialize);
}
void ChunkArchive::SerializeLoot(int id, void* records, void*) {
    SerializeItemRecords<itemdb::QuestItemRecord>(this, id, records,
                                    &DestroyItemRecord<itemdb::QuestItemRecord>, recordvec::ResizeUntyped<recordvec::QuestItemVector>, &itemdb::QuestItemRecord::Serialize);
}
void ChunkArchive::SerializeQuestItems(int id, void* records, void*) {
    SerializeItemRecords<itemdb::LootItemRecord>(this, id, records,
                                    &DestroyItemRecord<itemdb::LootItemRecord>, recordvec::ResizeUntyped<recordvec::LootVector>, &itemdb::LootItemRecord::Serialize);
}
EI_REWRITTEN(0x006A7C20, ChunkArchive::SerializeQuickItems);
EI_REWRITTEN(0x006A7D20, ChunkArchive::SerializeLoot);
EI_REWRITTEN(0x006A7E10, ChunkArchive::SerializeQuestItems);

void ChunkArchive::SerializeMaterials(int id, void* records, void*) {
    SerializeItemRecords<itemdb::MaterialRecordHead>(this, id, records,
                                    &itemdb::MaterialRecordHead::Destruct, recordvec::ResizeUntyped<recordvec::MaterialVector>, &itemdb::MaterialRecordHead::Serialize);
}
void ChunkArchive::SerializeWeapons(int id, void* records, void*) {
    SerializeItemRecords<itemdb::WeaponRecord>(this, id, records,
                                    &DestroyItemRecord<itemdb::WeaponRecord>, recordvec::ResizeUntyped<recordvec::WeaponVector>, &itemdb::WeaponRecord::Serialize);
}
void ChunkArchive::SerializeArmors(int id, void* records, void*) {
    SerializeItemRecords<itemdb::ArmorRecord>(this, id, records,
                                    &DestroyItemRecord<itemdb::ArmorRecord>, recordvec::ResizeUntyped<recordvec::ArmorVector>, &itemdb::ArmorRecord::Serialize);
}
EI_REWRITTEN(0x006A7940, ChunkArchive::SerializeMaterials);
EI_REWRITTEN(0x006A7A30, ChunkArchive::SerializeWeapons);
EI_REWRITTEN(0x006A7B30, ChunkArchive::SerializeArmors);

EI_REWRITTEN(0x006A6E50, itemdb::QuickItemTable::Load);
EI_REWRITTEN(0x006A7110, itemdb::QuestItemTable::Load);
EI_REWRITTEN(0x006A73B0, itemdb::LootItemTable::Load);

// --- The spell database's vectors and records ----------------------------------------------

namespace {

// The vectors' resize: recordvec::ResizeUntyped.

// A string list as chunk `id`: one string field 1 per entry; loading erases the list and resizes it
// to the field count first.
void StringListField(ChunkArchive* ar, int id, stlp::vector<stlp::string>* list) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        list->EraseRange<stlp::StringElement>(list->begin(), list->end());
        count = ar->CountChunks(1);
        static_cast<stlp::VectorAbi<stlp::string, stlp::StringElement>*>(list)->ResizeDefault(static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(list->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ar->StringField(1, &list->Start()[i]);
    }
    ar->LeaveChunk();
}

} // namespace

void itemdb::SpellRecord::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, &name);
    ar->CStringField(1, &code);
    ar->CStringField(2, &text08);
    ar->Field(3, &price, 4);
    ar->Field(4, &castType, 4);
    ar->Field(5, &mana, 4);
    ar->Field(6, &complexity, 4);
    ar->Field(7, &speed, 4);
    ar->Field(8, &range, 4);
    ar->Field(9, &area, 4);
    ar->Field(10, &damage, 4);
    ar->Field(11, &targetKind, 4);
    ar->Field(12, &targetCount, 4);
    ar->Field(13, &duration, 4);
    ar->Field(14, &value3C, 4);
    ar->Field(15, &needsSight, 4);
    ar->IntVectorField(16, &list48, ar);
    ar->IntVectorField(17, &list54, ar);
    ar->Field(18, &icon, 4);
    ar->Field(19, &school, 4);
    ar->Field(20, bytes78, 5);
    ar->Field(21, &skill, 4);
    ar->Field(22, &classMask, 4);
    for (int i = 0; i < 5; ++i) ar->Field(23 + i, &extraValues[i], 4);
}
EI_REWRITTEN(0x00683B80, itemdb::SpellRecord::Serialize);

void itemdb::SpellModifierRecord::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, &name);
    ar->Field(1, &code[0], 4);
    ar->Field(2, &price, 4);
    ar->Field(3, &kind, 4);
    ar->Field(4, &mana, 4);
    ar->Field(5, &strength, 4);
    ar->Field(6, &skill, 4);
    ar->CStringField(7, &text1C);
    ar->Field(8, &classMask, 4);
}
EI_REWRITTEN(0x00683AF0, itemdb::SpellModifierRecord::Serialize);

void itemdb::SpellListRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &spell);
    StringListField(ar, 2, &required);
    StringListField(ar, 3, &optional);
    ar->StringField(4, &repeated);
    ar->Field(5, &classMask, 4);
}
EI_REWRITTEN(0x00683D40, itemdb::SpellListRecord::Serialize);

void ChunkArchive::IntVectorField(int id, stlp::vector<int>* values, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        // erase(begin, end) (the tail copy is empty), then resize(count, 0): the vector is empty, so always
        // the fill insert (the shrinking branch is not reached).
        values->Finish() = values->Start();
        count = CountChunks(1);
        int zero = 0;
        values->FillInsert<stlp::PlainElement<int>>(values->Finish(), static_cast<unsigned>(count), zero);
    } else {
        count = static_cast<int>(values->size());
    }
    for (int i = 0; i < count; ++i) {
        Current()->occurrence = i + 1;
        Field(1, &values->Start()[i], 4);
    }
    LeaveChunk();
}
EI_REWRITTEN(0x00683EB0, ChunkArchive::IntVectorField);

void ChunkArchive::SerializeSpells(int id, void* records, void*) {
    SerializeItemRecords<itemdb::SpellRecord>(this, id, records,
                                    &itemdb::SpellRecord::Destruct, recordvec::ResizeUntyped<recordvec::SpellVector>, &itemdb::SpellRecord::Serialize);
}
void ChunkArchive::SerializeSpellModifiers(int id, void* records, void*) {
    SerializeItemRecords<itemdb::SpellModifierRecord>(this, id, records,
                                    &itemdb::SpellModifierRecord::Destruct, recordvec::ResizeUntyped<recordvec::SpellModifierVector>, &itemdb::SpellModifierRecord::Serialize);
}
void ChunkArchive::SerializeSpellLists(int id, void* records, void*) {
    SerializeItemRecords<itemdb::SpellListRecord>(this, id, records,
                                    &itemdb::SpellListRecord::Destruct, recordvec::ResizeUntyped<recordvec::SpellListVector>, &itemdb::SpellListRecord::Serialize);
}
EI_REWRITTEN(0x00684EF0, ChunkArchive::SerializeSpells);
EI_REWRITTEN(0x00684FE0, ChunkArchive::SerializeSpellModifiers);
EI_REWRITTEN(0x00684C50, ChunkArchive::SerializeSpellLists);

// --- The database's destructor ---------------------------------------------------------------

namespace {

// One table: the name index (its clear, then its buckets; the materials' as a whole), the records in order,
// the vector's storage (STLport's deallocate of the capacity in whole records).
template <typename Record, typename Destroy>
void DestroyTable(itemdb::NamedRecordTable<Record>& table, Destroy destroy) {
    table.names.Destroy();
    Record* end = table.records.end();
    for (Record* record = table.records.begin(); record != end; ++record) std::invoke(destroy, record);
    if (table.records.begin() != nullptr)
        stlp::deallocate(table.records.begin(), table.records.Capacity() * sizeof(Record));
}

} // namespace

void itemdb::ItemDatabase::DestructBody() {
    DestroyTable(loot, &DestroyItemRecord<LootItemRecord>);
    DestroyTable(questItems, &DestroyItemRecord<QuestItemRecord>);
    DestroyTable(quickItems, &DestroyItemRecord<QuickItemRecord>);
    DestroyTable(armors, &DestroyItemRecord<ArmorRecord>);
    DestroyTable(weapons, &DestroyItemRecord<WeaponRecord>);
    DestroyTable(materials, &MaterialRecordHead::Destruct);
}
EI_REWRITTEN(0x006A6570, itemdb::ItemDatabase::DestructBody);
