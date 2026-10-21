// The item database's name-indexed tables read from an archive stream: quick items, quest items and
// loot (006A6E50 / 006A7110 / 006A73B0, from the database's load 006A67A0). See item_db.h
// (itemdb::IndexedRecordTable) and docs/notes/items_spells.md ("Databases and naming").
#include "item_db.h"

#include "chunk_archive.h"
#include "core_streams.h"
#include "record_vectors.h"

#include "ei/hook.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "lib/stlp_vector.h"

namespace {

using SerializeFn = void (ChunkArchive::*)(int, void*, void*);

// The records read through an archive, then each filed in the name index under its name (the
// index's find_or_insert of {name, null}: 00613B70 quick items, 006A7870 loot, 0069E2A0 quest
// items).
void LoadIndexed(itemdb::IndexedRecordTable* table, CStream* source, int field, int recordSize, SerializeFn serialize) {
    ChunkArchive archive;
    archive.ConstructLocal();
    archive.Open(source, 1, 1);
    (archive.*serialize)(field, table, nullptr);
    archive.Close(source);
    for (char* record = table->records.begin; record != table->records.end; record += recordSize) {
        stlp::string name(reinterpret_cast<const itemdb::ItemRecordHead*>(record)->text04);
        stlp::NamedSlot<char>(table->names, name) = record;
    }
    archive.DestructLocal();
}

} // namespace

namespace itemdb {

void IndexedRecordTable::LoadQuickItems(CStream* source, int field) {
    LoadIndexed(this, source, field, kQuickItemSize, &ChunkArchive::SerializeQuickItems);
}
void IndexedRecordTable::LoadQuestItems(CStream* source, int field) {
    LoadIndexed(this, source, field, kQuestItemSize, &ChunkArchive::SerializeLoot);
}
void IndexedRecordTable::LoadLoot(CStream* source, int field) {
    LoadIndexed(this, source, field, kLootSize, &ChunkArchive::SerializeQuestItems);
}

} // namespace itemdb

// --- A-H18: the tables' vector serialisers ----------------------------------------------------------

namespace {

using ResizeFn = void(__fastcall*)(void* records, void*, unsigned count);

// Per record type: the record's destructor (plain RET), the vector's resize (RET 4: new records
// default-constructed; 006A83F0, 006A85A0, 006A8790, 006A7F00, 006A8110, 006A8290) and the
// record's Serialize (RET 4).

// The body of the three instantiations: chunk `id` holding one chunk 1 per record; loading
// destroys the old records (erase(begin, end): the tail copy is empty) and resizes to the number
// of record chunks first.
template <typename Record, typename Destroy, typename Serialize>
void SerializeItemRecords(ChunkArchive* ar, int id, RecordTable* vector, int size, Destroy destroy,
                          ResizeFn resize, Serialize serialize) {
    if (!ar->EnterChunk(id)) return;
    auto& records = *vector;
    int count;
    if (ar->loading_) {
        char* end = records.end;
        for (char* p = records.begin; p != end; p += size) (reinterpret_cast<Record*>(p)->*destroy)();
        records.end = records.begin;
        count = ar->CountChunks(1);
        resize(vector, nullptr, static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(records.end - records.begin) / size;
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        char* record = records.begin + i * size;
        if (ar->EnterChunk(1)) {
            (reinterpret_cast<Record*>(record)->*serialize)(ar);
            ar->LeaveChunk();
        }
    }
    ar->LeaveChunk();
}

} // namespace

void ChunkArchive::SerializeQuickItems(int id, void* records, void*) {
    SerializeItemRecords<itemdb::QuickItemRecord>(this, id, static_cast<RecordTable*>(records), itemdb::kQuickItemSize,
                                    &itemdb::QuickItemRecord::Destruct, recordvec::ResizeUntyped<recordvec::QuickItemVector>, &itemdb::QuickItemRecord::Serialize);
}
void ChunkArchive::SerializeLoot(int id, void* records, void*) {
    SerializeItemRecords<itemdb::QuestItemRecord>(this, id, static_cast<RecordTable*>(records), itemdb::kQuestItemSize,
                                    &itemdb::QuestItemRecord::Destruct, recordvec::ResizeUntyped<recordvec::QuestItemVector>, &itemdb::QuestItemRecord::Serialize);
}
void ChunkArchive::SerializeQuestItems(int id, void* records, void*) {
    SerializeItemRecords<itemdb::LootItemRecord>(this, id, static_cast<RecordTable*>(records), itemdb::kLootSize,
                                    &itemdb::ItemRecordHead::DestructHead, recordvec::ResizeUntyped<recordvec::LootVector>, &itemdb::LootItemRecord::Serialize);
}
EI_REWRITTEN(0x006A7C20, ChunkArchive::SerializeQuickItems);
EI_REWRITTEN(0x006A7D20, ChunkArchive::SerializeLoot);
EI_REWRITTEN(0x006A7E10, ChunkArchive::SerializeQuestItems);

void ChunkArchive::SerializeMaterials(int id, void* records, void*) {
    SerializeItemRecords<itemdb::MaterialRecordHead>(this, id, static_cast<RecordTable*>(records), itemdb::kMaterialSize,
                                    &itemdb::MaterialRecordHead::Destruct, recordvec::ResizeUntyped<recordvec::MaterialVector>, &itemdb::MaterialRecordHead::Serialize);
}
void ChunkArchive::SerializeWeapons(int id, void* records, void*) {
    SerializeItemRecords<itemdb::WeaponRecord>(this, id, static_cast<RecordTable*>(records), itemdb::kWeaponSize,
                                    &itemdb::ItemRecordHead::DestructHead, recordvec::ResizeUntyped<recordvec::WeaponVector>, &itemdb::WeaponRecord::Serialize);
}
void ChunkArchive::SerializeArmors(int id, void* records, void*) {
    SerializeItemRecords<itemdb::ArmorRecord>(this, id, static_cast<RecordTable*>(records), itemdb::kArmorSize,
                                    &itemdb::ItemRecordHead::DestructHead, recordvec::ResizeUntyped<recordvec::ArmorVector>, &itemdb::ArmorRecord::Serialize);
}
EI_REWRITTEN(0x006A7940, ChunkArchive::SerializeMaterials);
EI_REWRITTEN(0x006A7A30, ChunkArchive::SerializeWeapons);
EI_REWRITTEN(0x006A7B30, ChunkArchive::SerializeArmors);

EI_REWRITTEN(0x006A6E50, itemdb::IndexedRecordTable::LoadQuickItems);
EI_REWRITTEN(0x006A7110, itemdb::IndexedRecordTable::LoadQuestItems);
EI_REWRITTEN(0x006A73B0, itemdb::IndexedRecordTable::LoadLoot);

// --- A-H21: the spell database's vectors and records ---------------------------------------------

namespace {

// The vectors' resize (00685730, 006858D0, 00684D60): recordvec::ResizeUntyped.

// A string list as chunk `id`: one string field 1 per entry; loading erases the list and resizes it
// to the field count first.
void StringListField(ChunkArchive* ar, int id, stlp::vector<stlp::string>* list) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        list->EraseRange<stlp::StringElement>(list->begin(), list->end());
        count = ar->CountChunks(1);
        stlp::VectorAbi<stlp::string, stlp::StringElement>::ResizeDefault(list, nullptr, static_cast<unsigned>(count));
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
    for (int i = 0; i < 9; ++i) ar->Field(6 + i, &effectValues[i], 4);
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
    ar->StringField(1, &name);
    StringListField(ar, 2, &list0C);
    StringListField(ar, 3, &list18);
    ar->StringField(4, &text24);
    ar->Field(5, &value30, 4);
}
EI_REWRITTEN(0x00683D40, itemdb::SpellListRecord::Serialize);

void ChunkArchive::IntVectorField(int id, stlp::vector<int>* values, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        // erase(begin, end) (the tail copy 00500040 is empty), then resize(count, 0): the vector is
        // empty, so always the fill insert (the shrinking branch, 0055C2C0, is not reached).
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
    SerializeItemRecords<itemdb::SpellRecord>(this, id, static_cast<RecordTable*>(records), itemdb::kSpellSize,
                                    &itemdb::SpellRecord::Destruct, recordvec::ResizeUntyped<recordvec::SpellVector>, &itemdb::SpellRecord::Serialize);
}
void ChunkArchive::SerializeSpellModifiers(int id, void* records, void*) {
    SerializeItemRecords<itemdb::SpellModifierRecord>(this, id, static_cast<RecordTable*>(records), itemdb::kSpellModifierSize,
                                    &itemdb::SpellModifierRecord::Destruct, recordvec::ResizeUntyped<recordvec::SpellModifierVector>, &itemdb::SpellModifierRecord::Serialize);
}
void ChunkArchive::SerializeSpellLists(int id, void* records, void*) {
    SerializeItemRecords<itemdb::SpellListRecord>(this, id, static_cast<RecordTable*>(records), sizeof(itemdb::SpellListRecord),
                                    &itemdb::SpellListRecord::Destruct, recordvec::ResizeUntyped<recordvec::SpellListVector>, &itemdb::SpellListRecord::Serialize);
}
EI_REWRITTEN(0x00684EF0, ChunkArchive::SerializeSpells);
EI_REWRITTEN(0x00684FE0, ChunkArchive::SerializeSpellModifiers);
EI_REWRITTEN(0x00684C50, ChunkArchive::SerializeSpellLists);

// --- A-H18: the database's destructor ---------------------------------------------------------------

namespace {

// One table: the name index (its clear, then its buckets: 006A6D00 / 004AEDA0 the weapons, 006A6D70
// / 004AEF50 the armors, 006A6DE0 / 004AF100 the quick items, 006A70A0 / 005843A0 the quest items,
// 006A7340 / 00584450 the loot; the materials' destructor 006A6C20 as a whole), the records in
// order, the vector's storage (STLport's deallocate of the capacity in whole records).
template <typename Record, typename Destroy>
void DestroyTable(itemdb::IndexedRecordTable& table, int size, Destroy destroy) {
    table.names.Destroy();
    char* end = table.records.end;
    for (char* record = table.records.begin; record != end; record += size)
        (reinterpret_cast<Record*>(record)->*destroy)();
    if (table.records.begin != nullptr)
        stlp::deallocate(table.records.begin,
                         static_cast<std::size_t>((table.capacityEnd - table.records.begin) / size * size));
}

} // namespace

void itemdb::ItemDatabase::DestructBody() {
    DestroyTable<LootItemRecord>(loot, kLootSize, &ItemRecordHead::DestructHead);
    DestroyTable<QuestItemRecord>(questItems, kQuestItemSize, &QuestItemRecord::Destruct);
    DestroyTable<QuickItemRecord>(quickItems, kQuickItemSize, &QuickItemRecord::Destruct);
    DestroyTable<ArmorRecord>(armors, kArmorSize, &ItemRecordHead::DestructHead);
    DestroyTable<WeaponRecord>(weapons, kWeaponSize, &ItemRecordHead::DestructHead);
    DestroyTable<MaterialRecordHead>(materials, kMaterialSize, &MaterialRecordHead::Destruct);
}
EI_REPLACE_METHOD(0x006A6570, itemdb::ItemDatabase::DestructBody);
