// The item database's name-indexed tables read from an archive stream: quick items, loot and
// quest items (006A6E50 / 006A7110 / 006A73B0, from the database's load 006A67A0). See item_db.h
// (itemdb::IndexedRecordTable) and docs/notes/items_spells.md (A-H17).
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
// wrappers run.
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
void IndexedRecordTable::LoadLoot(CStream* source, int field) {
    LoadIndexed(this, source, field, kLootSize, SerializeLootAt, &LootIndexInsertOriginal);
}
void IndexedRecordTable::LoadQuestItems(CStream* source, int field) {
    LoadIndexed(this, source, field, kQuestItemSize, SerializeQuestItemsAt, &QuestItemIndexInsertOriginal);
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

// The body of the three instantiations: chunk `id` holding one chunk 1 per record; loading
// destroys the old records (erase(begin, end): the tail copy is empty) and resizes to the number
// of record chunks first.
void SerializeItemRecords(ChunkArchive* ar, int id, itemdb::IndexedRecordTable* table, int size, RecordFn destroy,
                          ResizeFn resize, RecordSerializeFn serialize) {
    if (!ar->EnterChunk(id)) return;
    auto& records = table->records;
    int count;
    if (ar->loading_) {
        char* end = records.end;
        for (char* p = records.begin; p != end; p += size) destroy(p);
        records.end = records.begin;
        count = ar->CountChunks(1);
        resize(table, nullptr, static_cast<unsigned>(count));
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
    SerializeItemRecords(this, id, static_cast<itemdb::IndexedRecordTable*>(records), itemdb::kQuickItemSize,
                         QuickItemDestroy, QuickItemsResize, QuickItemSerialize);
}
void ChunkArchive::SerializeLoot(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<itemdb::IndexedRecordTable*>(records), itemdb::kLootSize, LootDestroy,
                         LootResize, LootSerialize);
}
void ChunkArchive::SerializeQuestItems(int id, void* records, void*) {
    SerializeItemRecords(this, id, static_cast<itemdb::IndexedRecordTable*>(records), itemdb::kQuestItemSize,
                         QuestItemDestroy, QuestItemsResize, QuestItemSerialize);
}
EI_REPLACE_METHOD(0x006A7C20, ChunkArchive::SerializeQuickItems);
EI_REPLACE_METHOD(0x006A7D20, ChunkArchive::SerializeLoot);
EI_REPLACE_METHOD(0x006A7E10, ChunkArchive::SerializeQuestItems);

EI_REPLACE_METHOD(0x006A6E50, itemdb::IndexedRecordTable::LoadQuickItems);
EI_REPLACE_METHOD(0x006A7110, itemdb::IndexedRecordTable::LoadLoot);
EI_REPLACE_METHOD(0x006A73B0, itemdb::IndexedRecordTable::LoadQuestItems);

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
__declspec(naked) void __fastcall MaterialDestroy(void*) EI_ORIGINAL(0x006A88F0)
__declspec(naked) void __fastcall WeaponDestroy(void*) EI_ORIGINAL(0x006A8950)
__declspec(naked) void __fastcall ArmorDestroy(void*) EI_ORIGINAL(0x006A89D0)

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
    DestroyTable(questItems, kQuestItemSize, QuestItemIndexClear, QuestItemBucketsDestroy, QuestItemDestroy);
    DestroyTable(loot, kLootSize, LootIndexClear, LootBucketsDestroy, LootDestroy);
    DestroyTable(quickItems, kQuickItemSize, QuickItemIndexClear, QuickItemBucketsDestroy, QuickItemDestroy);
    DestroyTable(armors, kArmorSize, ArmorIndexClear, ArmorBucketsDestroy, ArmorDestroy);
    DestroyTable(weapons, kWeaponSize, WeaponIndexClear, WeaponBucketsDestroy, WeaponDestroy);
    DestroyTable(materials, kMaterialSize, MaterialIndexDestroy, nullptr, MaterialDestroy);
}
EI_REPLACE_METHOD(0x006A6570, itemdb::ItemDatabase::DestructBody);
