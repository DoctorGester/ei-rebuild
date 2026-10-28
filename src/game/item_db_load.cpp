// The item database's name-indexed tables read from an archive stream: quick items, quest items and loot. See
// item_db.h (itemdb::NamedRecordTable) and docs/notes/items_spells.md ("Databases and naming").
#include "item_db.h"

#include "chunk_archive.h"
#include "core_streams.h"

#include "lib/stlp.h"
#include "lib/stlp_hash.h"

#include <functional>
#include <type_traits>
#include <vector>

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
    for (Record& record : table->records) {
        std::string name(record.text04.c_str());
        stlp::NamedSlot<Record>(table->names, name) = &record;
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

void ChunkArchive::SerializeQuickItems(int id, std::vector<itemdb::QuickItemRecord>* records, void*) {
    RecordChunks(this, id, records, &itemdb::QuickItemRecord::Serialize);
}
void ChunkArchive::SerializeLoot(int id, std::vector<itemdb::QuestItemRecord>* records, void*) {
    RecordChunks(this, id, records, &itemdb::QuestItemRecord::Serialize);
}
void ChunkArchive::SerializeQuestItems(int id, std::vector<itemdb::LootItemRecord>* records, void*) {
    RecordChunks(this, id, records, &itemdb::LootItemRecord::Serialize);
}

void ChunkArchive::SerializeMaterials(int id, std::vector<itemdb::MaterialRecordHead>* records, void*) {
    RecordChunks(this, id, records, &itemdb::MaterialRecordHead::Serialize);
}
void ChunkArchive::SerializeWeapons(int id, std::vector<itemdb::WeaponRecord>* records, void*) {
    RecordChunks(this, id, records, &itemdb::WeaponRecord::Serialize);
}
void ChunkArchive::SerializeArmors(int id, std::vector<itemdb::ArmorRecord>* records, void*) {
    RecordChunks(this, id, records, &itemdb::ArmorRecord::Serialize);
}

// --- The spell database's vectors and records ----------------------------------------------

namespace {

// A string list as chunk `id`: one string field 1 per entry; loading erases the list and resizes it
// to the field count first.
void StringListField(ChunkArchive* ar, int id, std::vector<std::string>* list) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading_) {
        list->clear();
        count = ar->CountChunks(1);
        list->resize(static_cast<unsigned>(count));
    } else {
        count = static_cast<int>(list->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ar->StringField(1, &(*list)[i]);
    }
    ar->LeaveChunk();
}

} // namespace

void itemdb::SpellRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &code);
    ar->StringField(2, &text08);
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

void itemdb::SpellModifierRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, &code[0], 4);
    ar->Field(2, &price, 4);
    ar->Field(3, &kind, 4);
    ar->Field(4, &mana, 4);
    ar->Field(5, &strength, 4);
    ar->Field(6, &skill, 4);
    ar->StringField(7, &text1C);
    ar->Field(8, &classMask, 4);
}

void itemdb::SpellListRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &spell);
    StringListField(ar, 2, &required);
    StringListField(ar, 3, &optional);
    ar->StringField(4, &repeated);
    ar->Field(5, &classMask, 4);
}

void ChunkArchive::IntVectorField(int id, std::vector<int>* values, void*) {
    if (!EnterChunk(id)) return;
    int count;
    if (loading_) {
        // erase(begin, end) (the tail copy is empty), then resize(count, 0): the vector is empty, so always
        // the fill insert (the shrinking branch is not reached).
        values->clear();
        count = CountChunks(1);
        int zero = 0;
        values->insert(values->end(), static_cast<unsigned>(count), zero);
    } else {
        count = static_cast<int>(values->size());
    }
    for (int i = 0; i < count; ++i) {
        Current()->occurrence = i + 1;
        Field(1, &(*values)[i], 4);
    }
    LeaveChunk();
}

void ChunkArchive::SerializeSpells(int id, std::vector<itemdb::SpellRecord>* records, void*) {
    RecordChunks(this, id, records, &itemdb::SpellRecord::Serialize);
}
void ChunkArchive::SerializeSpellModifiers(int id, std::vector<itemdb::SpellModifierRecord>* records, void*) {
    RecordChunks(this, id, records, &itemdb::SpellModifierRecord::Serialize);
}
void ChunkArchive::SerializeSpellLists(int id, std::vector<itemdb::SpellListRecord>* records, void*) {
    RecordChunks(this, id, records, &itemdb::SpellListRecord::Serialize);
}

// --- The database's destructor ---------------------------------------------------------------

void itemdb::ItemDatabase::DestructBody() {
    loot.Destruct();
    questItems.Destruct();
    quickItems.Destruct();
    armors.Destruct();
    weapons.Destruct();
    materials.Destruct();
}
