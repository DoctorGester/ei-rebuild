// The item database's name-indexed tables read from an archive stream: quick items, quest items and loot. See
// item_db.h (itemdb::NamedRecordTable) and docs/notes/items_spells.md ("Databases and naming").
#include "item_db.h"

#include "chunk_archive.h"
#include "core_streams.h"


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
        archive.SerializeQuickItems(field, &table->records);
    else if constexpr (std::is_same_v<Record, itemdb::QuestItemRecord>)
        archive.SerializeLoot(field, &table->records);
    else
        archive.SerializeQuestItems(field, &table->records);
    archive.Close(source);
    for (Record& record : table->records) {
        std::string name = record.name;
        table->names[name] = &record;
    }
}

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

void ChunkArchive::SerializeQuickItems(int id, std::vector<itemdb::QuickItemRecord>* records) {
    RecordChunks(this, id, records, &itemdb::QuickItemRecord::Serialize);
}
void ChunkArchive::SerializeLoot(int id, std::vector<itemdb::QuestItemRecord>* records) {
    RecordChunks(this, id, records, &itemdb::QuestItemRecord::Serialize);
}
void ChunkArchive::SerializeQuestItems(int id, std::vector<itemdb::LootItemRecord>* records) {
    RecordChunks(this, id, records, &itemdb::LootItemRecord::Serialize);
}

void ChunkArchive::SerializeMaterials(int id, std::vector<itemdb::MaterialRecordHead>* records) {
    RecordChunks(this, id, records, &itemdb::MaterialRecordHead::Serialize);
}
void ChunkArchive::SerializeWeapons(int id, std::vector<itemdb::WeaponRecord>* records) {
    RecordChunks(this, id, records, &itemdb::WeaponRecord::Serialize);
}
void ChunkArchive::SerializeArmors(int id, std::vector<itemdb::ArmorRecord>* records) {
    RecordChunks(this, id, records, &itemdb::ArmorRecord::Serialize);
}

// --- The spell database's vectors and records ----------------------------------------------

void itemdb::SpellRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &code);
    ar->StringField(2, &unreadText);
    ar->Field(3, &price);
    ar->Field(4, &castType);
    ar->Field(5, &mana);
    ar->Field(6, &complexity);
    ar->Field(7, &speed);
    ar->Field(8, &range);
    ar->Field(9, &area);
    ar->Field(10, &damage);
    ar->Field(11, &targetKind);
    ar->Field(12, &targetCount);
    ar->Field(13, &duration);
    ar->Field(14, &castTime);
    ar->Field(15, &needsSight);
    ar->IntVectorField(16, &unreadIdListA);
    ar->IntVectorField(17, &unreadIdListB);
    ar->Field(18, &icon);
    ar->Field(19, &school);
    ar->Field(20, allowedModifiers, 5);
    ar->Field(21, &skill);
    ar->Field(22, &classMask);
    for (int i = 0; i < 5; ++i) ar->Field(23 + i, &extraValues[i]);
}

void itemdb::SpellModifierRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, &code[0], 4);
    ar->Field(2, &price);
    ar->Field(3, &kind);
    ar->Field(4, &mana);
    ar->Field(5, &strength);
    ar->Field(6, &skill);
    ar->StringField(7, &unreadText);
    ar->Field(8, &classMask);
}

void itemdb::SpellListRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &spell);
    StringListField(ar, 2, &required);
    StringListField(ar, 3, &optional);
    ar->StringField(4, &repeated);
    ar->Field(5, &classMask);
}

void ChunkArchive::IntVectorField(int id, std::vector<int>* values) {
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

void ChunkArchive::SerializeSpells(int id, std::vector<itemdb::SpellRecord>* records) {
    RecordChunks(this, id, records, &itemdb::SpellRecord::Serialize);
}
void ChunkArchive::SerializeSpellModifiers(int id, std::vector<itemdb::SpellModifierRecord>* records) {
    RecordChunks(this, id, records, &itemdb::SpellModifierRecord::Serialize);
}
void ChunkArchive::SerializeSpellLists(int id, std::vector<itemdb::SpellListRecord>* records) {
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
