// Checks for item_db_load.cpp (A-H17; every scenario: the item database loads at startup, in a
// Rebirth launch too, where its patched load 006A67A0 still calls these through their addresses).
// Loading twice would replace the records, so ours runs alone; afterwards every record's name
// finds, in the index, a record of that name (the last of equal names).
#include "verify.h"

#include "../../src/game/chunk_archive.h"
#include "../../src/game/core_streams.h"
#include "../../src/game/item_db.h"
#include "../../src/game/originals_library.h"
#include "../../src/lib/stlp.h"

#include <cstring>

namespace {
void* s_unused;

struct NamedRecord {
    stlp::string name;
    char* record;
};
using FindFn = void*(__fastcall*)(void*, void*, const void*);

const char* NameOf(const char* record) { return reinterpret_cast<const itemdb::ItemRecordHead*>(record)->text04; }

void CheckIndex(const char* label, itemdb::IndexedRecordTable* table, int recordSize, FindFn find) {
    bool ok = true;
    int count = 0;
    for (char* record = table->records.begin; ok && record != table->records.end; record += recordSize, ++count) {
        NamedRecord key{stlp::string(NameOf(record)), nullptr};
        auto* found = static_cast<NamedRecord*>(find(table->index, nullptr, &key));
        ok = found->record != nullptr && std::strcmp(NameOf(found->record), NameOf(record)) == 0;
    }
    ei::verify::Check(label, true, ok, "%d records", count);
}

void __fastcall LoadQuickItemsVerify(itemdb::IndexedRecordTable* self, void*, CStream* source, int field) {
    self->LoadQuickItems(source, field);
    CheckIndex("IndexedRecordTable::LoadQuickItems", self, itemdb::kQuickItemSize, &QuickItemIndexInsertOriginal);
}
void __fastcall LoadLootVerify(itemdb::IndexedRecordTable* self, void*, CStream* source, int field) {
    self->LoadLoot(source, field);
    CheckIndex("IndexedRecordTable::LoadLoot", self, itemdb::kLootSize, &LootIndexInsertOriginal);
}
void __fastcall LoadQuestItemsVerify(itemdb::IndexedRecordTable* self, void*, CStream* source, int field) {
    self->LoadQuestItems(source, field);
    CheckIndex("IndexedRecordTable::LoadQuestItems", self, itemdb::kQuestItemSize, &QuestItemIndexInsertOriginal);
}
} // namespace

EI_VERIFY(0x006A6E50, LoadQuickItemsVerify, 0, s_unused);
EI_VERIFY(0x006A7110, LoadLootVerify, 0, s_unused);
EI_VERIFY(0x006A73B0, LoadQuestItemsVerify, 0, s_unused);

// A-H18: the vector serialisers 006A7C20 / 006A7D20 / 006A7E10 (the loads above call them through
// their addresses; every scenario): ours only. When loading, the record count afterwards must equal
// the number of record chunks, counted first with the archive's own calls (EnterChunk / CountChunks /
// LeaveChunk leave it as it was).
namespace {
template <void (ChunkArchive::*Fn)(int, void*, void*)>
void SerializeItemsVerify(const char* name, ChunkArchive* ar, int id, void* records, void* unused, int size) {
    int expected = -1;
    if (ar->loading_ && ar->EnterChunk(id)) {
        expected = ar->CountChunks(1);
        ar->LeaveChunk();
    }
    (ar->*Fn)(id, records, unused);
    if (expected >= 0) {
        const auto& table = static_cast<itemdb::IndexedRecordTable*>(records)->records;
        int actual = static_cast<int>(table.end - table.begin) / size;
        ei::verify::Check(name, expected, actual, "chunk %d: %d records vs %d", id, expected, actual);
    }
}
void __fastcall SerializeQuickItemsVerify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeItemsVerify<&ChunkArchive::SerializeQuickItems>("ChunkArchive::SerializeQuickItems", ar, id, records, unused,
                                                             itemdb::kQuickItemSize);
}
void __fastcall SerializeLootVerify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeItemsVerify<&ChunkArchive::SerializeLoot>("ChunkArchive::SerializeLoot", ar, id, records, unused,
                                                       itemdb::kLootSize);
}
void __fastcall SerializeQuestItemsVerify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeItemsVerify<&ChunkArchive::SerializeQuestItems>("ChunkArchive::SerializeQuestItems", ar, id, records, unused,
                                                             itemdb::kQuestItemSize);
}
} // namespace
EI_VERIFY(0x006A7C20, SerializeQuickItemsVerify, 0, s_unused);
EI_VERIFY(0x006A7D20, SerializeLootVerify, 0, s_unused);
EI_VERIFY(0x006A7E10, SerializeQuestItemsVerify, 0, s_unused);

// A-H20: the same for 006A7940 / 006A7A30 / 006A7B30 (materials, weapons, armor; the database's
// load 006A67A0 at startup, every scenario).
namespace {
void __fastcall SerializeMaterialsVerify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeItemsVerify<&ChunkArchive::SerializeMaterials>("ChunkArchive::SerializeMaterials", ar, id, records, unused,
                                                            itemdb::kMaterialSize);
}
void __fastcall SerializeWeaponsVerify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeItemsVerify<&ChunkArchive::SerializeWeapons>("ChunkArchive::SerializeWeapons", ar, id, records, unused,
                                                          itemdb::kWeaponSize);
}
void __fastcall SerializeArmorsVerify(ChunkArchive* ar, void*, int id, void* records, void* unused) {
    SerializeItemsVerify<&ChunkArchive::SerializeArmors>("ChunkArchive::SerializeArmors", ar, id, records, unused,
                                                         itemdb::kArmorSize);
}
} // namespace
EI_VERIFY(0x006A7940, SerializeMaterialsVerify, 0, s_unused);
EI_VERIFY(0x006A7A30, SerializeWeaponsVerify, 0, s_unused);
EI_VERIFY(0x006A7B30, SerializeArmorsVerify, 0, s_unused);

// A-H18: the database's destructor 006A6570 (exit, through atexit 006A6560; every scenario that
// lets the game exit): ours only; it must return (the tables are not used afterwards).
namespace {
void __fastcall ItemDatabaseDestructVerify(itemdb::ItemDatabase* self) {
    self->DestructBody();
    ei::verify::Check("ItemDatabase::DestructBody", true, true, "database %p", self);
}
} // namespace
EI_VERIFY(0x006A6570, ItemDatabaseDestructVerify, 0, s_unused);
