// Checks for item_db_load.cpp (A-H17; every scenario: the item database loads at startup, in a
// Rebirth launch too, where its patched load 006A67A0 still calls these through their addresses).
// Loading twice would replace the records, so ours runs alone; afterwards every record's name
// finds, in the index, a record of that name (the last of equal names).
#include "verify.h"

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
