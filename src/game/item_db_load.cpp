// The item database's name-indexed tables read from an archive stream: quick items, quest items and
// loot (006A6E50 / 006A7110 / 006A73B0, from the database's load 006A67A0). See item_db.h
// (itemdb::IndexedRecordTable) and docs/notes/items_spells.md ("Databases and naming").
#include "item_db.h"

#include "chunk_archive.h"
#include "core_streams.h"
#include "originals_library.h"

#include "ei/hook.h"
#include "lib/stlp.h"

namespace {

// A pair of a name index (pair<const string, record*>), as the index's find_or_insert takes it and
// returns the stored one.
struct NamedRecord {
    stlp::string name;
    char* record;
};
using FindOrInsertFn = void*(__fastcall*)(void* index, void*, const void* pair);
using SerializeFn = void(__fastcall*)(ChunkArchive*, void*, int, void*, void*);

// The vector serialisers (rewritten below, A-H18) through their addresses, so that their verify
// wrappers run. The forwarders here keep their first names, in which "Loot" is the quest item
// table (0x7C records) and "QuestItem" the loot table (0x78).
__declspec(naked) void __fastcall SerializeQuickItemsAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x006A7C20)
__declspec(naked) void __fastcall SerializeLootAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x006A7D20)
__declspec(naked) void __fastcall SerializeQuestItemsAt(ChunkArchive*, void*, int, void*, void*) EI_ORIGINAL(0x006A7E10)

void LoadIndexed(itemdb::IndexedRecordTable* table, CStream* source, int field, int recordSize, SerializeFn serialize,
                 FindOrInsertFn insert) {
    ChunkArchive archive;
    archive.ConstructLocal();
    archive.Open(source, 1, 1);
    serialize(&archive, nullptr, field, table, nullptr);
    archive.Close(source);
    for (char* record = table->records.begin; record != table->records.end; record += recordSize) {
        NamedRecord pair{stlp::string(reinterpret_cast<const itemdb::ItemRecordHead*>(record)->text04), nullptr};
        static_cast<NamedRecord*>(insert(table->index, nullptr, &pair))->record = record;
    }
    archive.DestructLocal();
}

} // namespace

namespace itemdb {

void IndexedRecordTable::LoadQuickItems(CStream* source, int field) {
    LoadIndexed(this, source, field, kQuickItemSize, SerializeQuickItemsAt, &QuickItemIndexInsertOriginal);
}
void IndexedRecordTable::LoadQuestItems(CStream* source, int field) {
    LoadIndexed(this, source, field, kQuestItemSize, SerializeLootAt, &LootIndexInsertOriginal);
}
void IndexedRecordTable::LoadLoot(CStream* source, int field) {
    LoadIndexed(this, source, field, kLootSize, SerializeQuestItemsAt, &QuestItemIndexInsertOriginal);
}

} // namespace itemdb

// --- A-H18: the tables' vector serialisers ----------------------------------------------------------

namespace {

using RecordFn = void(__fastcall*)(void* record);
using ResizeFn = void(__fastcall*)(void* records, void*, unsigned count);
using RecordSerializeFn = void(__fastcall*)(void* record, void*, ChunkArchive* ar);

// Per record type (STLport and record instantiations, not rewritten): the record's destructor
// (plain RET), the vector's resize (RET 4: new records default-constructed) and the record's
// Serialize (RET 4).
__declspec(naked) void __fastcall QuickItemDestroy(void*) EI_ORIGINAL(0x006A8A50)
__declspec(naked) void __fastcall LootDestroy(void*) EI_ORIGINAL(0x006A8AE0)
__declspec(naked) void __fastcall QuestItemDestroy(void*) EI_ORIGINAL(0x006A8BD0)
__declspec(naked) void __fastcall QuickItemsResize(void*, void*, unsigned) EI_ORIGINAL(0x006A83F0)
__declspec(naked) void __fastcall LootResize(void*, void*, unsigned) EI_ORIGINAL(0x006A85A0)
__declspec(naked) void __fastcall QuestItemsResize(void*, void*, unsigned) EI_ORIGINAL(0x006A8790)
__declspec(naked) void __fastcall QuickItemSerialize(void*, void*, ChunkArchive*) EI_ORIGINAL(0x0069F940)
__declspec(naked) void __fastcall LootSerialize(void*, void*, ChunkArchive*) EI_ORIGINAL(0x0069F9C0)
__declspec(naked) void __fastcall QuestItemSerialize(void*, void*, ChunkArchive*) EI_ORIGINAL(0x0069FA90)
// A-H20: materials, weapons and armor (the destructors are compiler-generated, library.csv).
__declspec(naked) void __fastcall MaterialDestroy(void*) EI_ORIGINAL(0x006A88F0)
__declspec(naked) void __fastcall WeaponDestroy(void*) EI_ORIGINAL(0x006A8950)
__declspec(naked) void __fastcall ArmorDestroy(void*) EI_ORIGINAL(0x006A89D0)
__declspec(naked) void __fastcall MaterialsResize(void*, void*, unsigned) EI_ORIGINAL(0x006A7F00)
__declspec(naked) void __fastcall WeaponsResize(void*, void*, unsigned) EI_ORIGINAL(0x006A8110)
__declspec(naked) void __fastcall ArmorsResize(void*, void*, unsigned) EI_ORIGINAL(0x006A8290)
__declspec(naked) void __fastcall MaterialSerialize(void*, void*, ChunkArchive*) EI_ORIGINAL(0x0069F620)
__declspec(naked) void __fastcall WeaponSerialize(void*, void*, ChunkArchive*) EI_ORIGINAL(0x0069F800)
__declspec(naked) void __fastcall ArmorSerialize(void*, void*, ChunkArchive*) EI_ORIGINAL(0x0069F8C0)

// The body of the three instantiations: chunk `id` holding one chunk 1 per record; loading
// destroys the old records (erase(begin, end): the tail copy is empty) and resizes to the number
// of record chunks first.
void SerializeItemRecords(ChunkArchive* ar, int id, RecordTable* vector, int size, RecordFn destroy,
                          ResizeFn resize, RecordSerializeFn serialize) {
    if (!ar->EnterChunk(id)) return;
    auto& records = *vector;
    int count;
    if (ar->loading_) {
        char* end = records.end;
        for (char* p = records.begin; p != end; p += size) destroy(p);
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
            serialize(record, nullptr, ar);
            ar->LeaveChunk();
        }
    }
    ar->LeaveChunk();
}

} // namespace

void ChunkArchive::SerializeQuickItems(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<RecordTable*>(records), itemdb::kQuickItemSize,
                         QuickItemDestroy, QuickItemsResize, QuickItemSerialize);
}
void ChunkArchive::SerializeLoot(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<RecordTable*>(records), itemdb::kQuestItemSize, LootDestroy,
                         LootResize, LootSerialize);
}
void ChunkArchive::SerializeQuestItems(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<RecordTable*>(records), itemdb::kLootSize,
                         QuestItemDestroy, QuestItemsResize, QuestItemSerialize);
}
EI_REPLACE_METHOD(0x006A7C20, ChunkArchive::SerializeQuickItems);
EI_REPLACE_METHOD(0x006A7D20, ChunkArchive::SerializeLoot);
EI_REPLACE_METHOD(0x006A7E10, ChunkArchive::SerializeQuestItems);

void ChunkArchive::SerializeMaterials(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<RecordTable*>(records), itemdb::kMaterialSize,
                         MaterialDestroy, MaterialsResize, MaterialSerialize);
}
void ChunkArchive::SerializeWeapons(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<RecordTable*>(records), itemdb::kWeaponSize,
                         WeaponDestroy, WeaponsResize, WeaponSerialize);
}
void ChunkArchive::SerializeArmors(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<RecordTable*>(records), itemdb::kArmorSize,
                         ArmorDestroy, ArmorsResize, ArmorSerialize);
}
EI_REPLACE_METHOD(0x006A7940, ChunkArchive::SerializeMaterials);
EI_REPLACE_METHOD(0x006A7A30, ChunkArchive::SerializeWeapons);
EI_REPLACE_METHOD(0x006A7B30, ChunkArchive::SerializeArmors);

EI_REPLACE_METHOD(0x006A6E50, itemdb::IndexedRecordTable::LoadQuickItems);
EI_REPLACE_METHOD(0x006A7110, itemdb::IndexedRecordTable::LoadQuestItems);
EI_REPLACE_METHOD(0x006A73B0, itemdb::IndexedRecordTable::LoadLoot);

// --- A-H21: the spell database's vectors and records ---------------------------------------------

namespace {

// Per record type (compiler-generated destructors, plain RET; the vectors' resize, RET 4; both in
// library.csv except 00589860).
__declspec(naked) void __fastcall SpellDestroy(void*) EI_ORIGINAL(0x00685AA0)
__declspec(naked) void __fastcall SpellModifierDestroy(void*) EI_ORIGINAL(0x00685B70)
__declspec(naked) void __fastcall SpellListDestroy(void*) EI_ORIGINAL(0x00589860)
__declspec(naked) void __fastcall SpellsResize(void*, void*, unsigned) EI_ORIGINAL(0x00685730)
__declspec(naked) void __fastcall SpellModifiersResize(void*, void*, unsigned) EI_ORIGINAL(0x006858D0)
__declspec(naked) void __fastcall SpellListsResize(void*, void*, unsigned) EI_ORIGINAL(0x00684D60)
// The records' Serialize (rewritten below), called through their addresses so that their verify
// wrappers run for every record.
__declspec(naked) void __fastcall SpellSerializeAt(void*, void*, ChunkArchive*) EI_ORIGINAL(0x00683B80)
__declspec(naked) void __fastcall SpellModifierSerializeAt(void*, void*, ChunkArchive*) EI_ORIGINAL(0x00683AF0)
__declspec(naked) void __fastcall SpellListSerializeAt(void*, void*, ChunkArchive*) EI_ORIGINAL(0x00683D40)
// 00683EB0 (RET 0xC), through its address for the same reason.
__declspec(naked) void __fastcall IntVectorFieldAt(ChunkArchive*, void*, int /*id*/, stlp::vector<int>*, void* /*unused*/)
    EI_ORIGINAL(0x00683EB0)
// STLport instantiations: vector<int>::_M_fill_insert(position, count, value) (RET 0xC),
// vector<string>::erase(first, last) (RET 8) and resize(count) (RET 4).
__declspec(naked) void __fastcall IntsFillInsert(stlp::vector<int>*, void*, int* /*position*/, unsigned /*count*/,
                                                 const int* /*value*/) EI_ORIGINAL(0x0055B030)
__declspec(naked) stlp::string* __fastcall StringsErase(stlp::vector<stlp::string>*, void*, stlp::string* /*first*/,
                                                        stlp::string* /*last*/) EI_ORIGINAL(0x00583CB0)
__declspec(naked) void __fastcall StringsResize(stlp::vector<stlp::string>*, void*, unsigned /*count*/) EI_ORIGINAL(0x004DC900)

// A string list as chunk `id`: one string field 1 per entry; loading erases the list and resizes it
// to the field count first.
void StringListField(ChunkArchive* ar, int id, stlp::vector<stlp::string>* list) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        StringsErase(list, nullptr, list->begin(), list->end());
        count = ar->CountChunks(1);
        StringsResize(list, nullptr, static_cast<unsigned>(count));
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
    IntVectorFieldAt(ar, nullptr, 16, &list48, ar);
    IntVectorFieldAt(ar, nullptr, 17, &list54, ar);
    ar->Field(18, &icon, 4);
    ar->Field(19, &school, 4);
    ar->Field(20, bytes78, 5);
    ar->Field(21, &skill, 4);
    ar->Field(22, &classMask, 4);
    for (int i = 0; i < 5; ++i) ar->Field(23 + i, &extraValues[i], 4);
}
EI_REPLACE_METHOD(0x00683B80, itemdb::SpellRecord::Serialize);

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
EI_REPLACE_METHOD(0x00683AF0, itemdb::SpellModifierRecord::Serialize);

void itemdb::SpellListRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &name);
    StringListField(ar, 2, &list0C);
    StringListField(ar, 3, &list18);
    ar->StringField(4, &text24);
    ar->Field(5, &value30, 4);
}
EI_REPLACE_METHOD(0x00683D40, itemdb::SpellListRecord::Serialize);

void ChunkArchive::IntVectorField(int id, stlp::vector<int>* values, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        // erase(begin, end) (the tail copy 00500040 is empty), then resize(count, 0): the vector is
        // empty, so always the fill insert (the shrinking branch, 0055C2C0, is not reached).
        values->Finish() = values->Start();
        count = CountChunks(1);
        int zero = 0;
        IntsFillInsert(values, nullptr, values->Finish(), static_cast<unsigned>(count), &zero);
    } else {
        count = static_cast<int>(values->size());
    }
    for (int i = 0; i < count; ++i) {
        Current()->occurrence = i + 1;
        Field(1, &values->Start()[i], 4);
    }
    LeaveChunk();
}
EI_REPLACE_METHOD(0x00683EB0, ChunkArchive::IntVectorField);

void ChunkArchive::SerializeSpells(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<RecordTable*>(records), itemdb::kSpellSize, SpellDestroy,
                         SpellsResize, SpellSerializeAt);
}
void ChunkArchive::SerializeSpellModifiers(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<RecordTable*>(records), itemdb::kSpellModifierSize,
                         SpellModifierDestroy, SpellModifiersResize, SpellModifierSerializeAt);
}
void ChunkArchive::SerializeSpellLists(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<RecordTable*>(records), sizeof(itemdb::SpellListRecord),
                         SpellListDestroy, SpellListsResize, SpellListSerializeAt);
}
EI_REPLACE_METHOD(0x00684EF0, ChunkArchive::SerializeSpells);
EI_REPLACE_METHOD(0x00684FE0, ChunkArchive::SerializeSpellModifiers);
EI_REPLACE_METHOD(0x00684C50, ChunkArchive::SerializeSpellLists);

// --- A-H18: the database's destructor ---------------------------------------------------------------

namespace {

using IndexFn = void(__fastcall*)(void* index);

// The name indexes' clear() (hash_map<string, record*>, per record type) and their bucket
// vectors' destructors; the materials' index destructor as a whole (006A6C20). The records'
// destructor bodies (the deleting destructors 006A9EF0..006A9F90 with flags 0 call these).
__declspec(naked) void __fastcall WeaponIndexClear(void*) EI_ORIGINAL(0x006A6D00)
__declspec(naked) void __fastcall ArmorIndexClear(void*) EI_ORIGINAL(0x006A6D70)
__declspec(naked) void __fastcall QuickItemIndexClear(void*) EI_ORIGINAL(0x006A6DE0)
__declspec(naked) void __fastcall LootIndexClear(void*) EI_ORIGINAL(0x006A70A0)
__declspec(naked) void __fastcall QuestItemIndexClear(void*) EI_ORIGINAL(0x006A7340)
__declspec(naked) void __fastcall WeaponBucketsDestroy(void*) EI_ORIGINAL(0x004AEDA0)
__declspec(naked) void __fastcall ArmorBucketsDestroy(void*) EI_ORIGINAL(0x004AEF50)
__declspec(naked) void __fastcall QuickItemBucketsDestroy(void*) EI_ORIGINAL(0x004AF100)
__declspec(naked) void __fastcall LootBucketsDestroy(void*) EI_ORIGINAL(0x005843A0)
__declspec(naked) void __fastcall QuestItemBucketsDestroy(void*) EI_ORIGINAL(0x00584450)
__declspec(naked) void __fastcall MaterialIndexDestroy(void*) EI_ORIGINAL(0x006A6C20)
// (The records' destructors are declared with the vector serialisers above.)

constexpr int kBucketsOffset = 4;   // the index's bucket vector

// One table: the index (clear, then its buckets; or one destructor), the records in order, the
// vector's storage (STLport's deallocate of the capacity in whole records).
void DestroyTable(itemdb::IndexedRecordTable& table, int size, IndexFn clear, IndexFn buckets, RecordFn destroy) {
    clear(table.index);
    if (buckets != nullptr) buckets(table.index + kBucketsOffset);
    char* end = table.records.end;
    for (char* record = table.records.begin; record != end; record += size) destroy(record);
    if (table.records.begin != nullptr)
        stlp::deallocate(table.records.begin,
                         static_cast<std::size_t>((table.capacityEnd - table.records.begin) / size * size));
}

} // namespace

void itemdb::ItemDatabase::DestructBody() {
    DestroyTable(loot, kLootSize, QuestItemIndexClear, QuestItemBucketsDestroy, QuestItemDestroy);
    DestroyTable(questItems, kQuestItemSize, LootIndexClear, LootBucketsDestroy, LootDestroy);
    DestroyTable(quickItems, kQuickItemSize, QuickItemIndexClear, QuickItemBucketsDestroy, QuickItemDestroy);
    DestroyTable(armors, kArmorSize, ArmorIndexClear, ArmorBucketsDestroy, ArmorDestroy);
    DestroyTable(weapons, kWeaponSize, WeaponIndexClear, WeaponBucketsDestroy, WeaponDestroy);
    DestroyTable(materials, kMaterialSize, MaterialIndexDestroy, nullptr, MaterialDestroy);
}
EI_REPLACE_METHOD(0x006A6570, itemdb::ItemDatabase::DestructBody);
