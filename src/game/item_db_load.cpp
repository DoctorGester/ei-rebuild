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
using SerializeFn = void (ChunkArchive::*)(int, void*, void*);

void LoadIndexed(itemdb::IndexedRecordTable* table, CStream* source, int field, int recordSize, SerializeFn serialize,
                 FindOrInsertFn insert) {
    ChunkArchive archive;
    archive.ConstructLocal();
    archive.Open(source, 1, 1);
    (archive.*serialize)(field, table, nullptr);
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
    LoadIndexed(this, source, field, kQuickItemSize, &ChunkArchive::SerializeQuickItems, &QuickItemIndexInsertOriginal);
}
void IndexedRecordTable::LoadLoot(CStream* source, int field) {
    LoadIndexed(this, source, field, kLootSize, &ChunkArchive::SerializeLoot, &LootIndexInsertOriginal);
}
void IndexedRecordTable::LoadQuestItems(CStream* source, int field) {
    LoadIndexed(this, source, field, kQuestItemSize, &ChunkArchive::SerializeQuestItems, &QuestItemIndexInsertOriginal);
}

} // namespace itemdb

__declspec(naked) void ChunkArchive::SerializeQuickItems(int, void*, void*) EI_ORIGINAL(0x006A7C20)
__declspec(naked) void ChunkArchive::SerializeLoot(int, void*, void*) EI_ORIGINAL(0x006A7D20)
__declspec(naked) void ChunkArchive::SerializeQuestItems(int, void*, void*) EI_ORIGINAL(0x006A7E10)

EI_REPLACE_METHOD(0x006A6E50, itemdb::IndexedRecordTable::LoadQuickItems);
EI_REPLACE_METHOD(0x006A7110, itemdb::IndexedRecordTable::LoadLoot);
EI_REPLACE_METHOD(0x006A73B0, itemdb::IndexedRecordTable::LoadQuestItems);
