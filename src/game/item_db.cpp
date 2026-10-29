// The item database: its records' Serialize, the tables' load and ItemDatabase::Load ("items.idb", at startup and
// at every switch of the resource database). The spell database is spell_db.cpp. See item_db.h and
// docs/notes/items_spells.md ("Databases and naming").
#include "item_db.h"

#include "chunk_archive.h"
#include "core_streams.h"
#include "database_file.h"
#include "unit_db.h"   // InlineStrings

#include "lib/crt.h"

#include <cstdint>
#include <string>

namespace {

// The chunks of items.idb.
constexpr int kMaterialsChunk = 1;
constexpr int kWeaponsChunk = 2;
constexpr int kArmorsChunk = 3;
constexpr int kQuickItemsChunk = 4;
constexpr int kQuestItemsChunk = 5;
constexpr int kLootChunk = 6;

// The text a table files a record under: a material's key, an item prototype's name.
const std::string& IndexKey(const itemdb::MaterialRecordHead& record) { return record.key; }
const std::string& IndexKey(const itemdb::ItemRecordHead& record) { return record.name; }

} // namespace

namespace itemdb {

// --- Records -------------------------------------------------------------------------------------

void MaterialRecordHead::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &key);
    ar->StringField(1, &soundName);
    ar->StringField(2, &name);
    ar->Field(3, &number);
    ar->Field(4, &unitPrice);
    ar->Field(5, &weightFactor);
    ar->Field(6, &magicPower);
    ar->Field(7, &magicLevel);
    ar->Field(8, &conditionFactor);
    ar->Field(9, &unreadField);
    ar->Field(10, &damageFactor);
    ar->Field(11, absorbFactors, sizeof(absorbFactors));
    ar->Field(12, &armourFlags);
    ar->Field(13, &classMask);
}

void ItemRecordHead::SerializeHead(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &family);
    ar->Field(2, &type);
    ar->StringField(3, &materialClass);
    ar->Field(4, &unreadHeadField);
    ar->Field(5, &number);
    ar->Field(6, &variant);
    ar->Field(7, &price);
    ar->Field(8, &weight);
    ar->Field(9, &unreadValue);
    ar->Field(10, &charge);
    ar->Field(11, &magicLevel);
    ar->Field(12, &condition);
    ar->Field(13, &materialUnits);
    ar->Field(14, &classMask);
    ar->Field(15, &buildable);
}

void WeaponRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category);
    ar->Field(0x16, unreadFields, 8);
    ar->Field(0x17, &reach);
    ar->Field(0x18, &damage);
    ar->Field(0x19, &damageSpread);
    ar->Field(0x1A, damageTypes, sizeof(damageTypes));
    ar->Field(0x1B, unreadFields + 8, 3);
    ar->Field(0x1C, skillBonus, 8);   // skillBonus and the first six bytes after it
    ar->Field(0x1D, &attackBonus);
    ar->Field(0x1E, &defenceBonus);
}

void ArmorRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 0x24);           // the factor (category), absorbFirst and the flags
    ar->Field(0x16, &othersFactor, 0x24);       // the factor, absorbOthers and the flags
    ar->Field(0x17, &showsWounds);
    ar->Field(0x18, &layer);
    ar->Field(0x19, unreadFields, 3);
    ar->Field(0x1A, unreadFields + 3, 8);
}

void QuickItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category);
    ar->Field(0x16, &actionValue);
    ar->Field(0x17, unreadFields, 4);
    ar->Field(0x18, unreadFields + 4, 4);
    ar->StringField(0x19, &spellText);
    ar->Field(0x1A, attributeBonus, 3);
    ar->Field(0x1B, unreadFields + 8, 8);
}

void QuestItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category);
    InlineStrings(ar, 0x16, &zones);
}

void LootItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category);
    ar->Field(0x16, &recordType);
    ar->Field(0x17, &index);
}

// --- Tables --------------------------------------------------------------------------------------

template <typename Record>
void NamedRecordTable<Record>::Load(ByteStream* source, int chunk) {
    ChunkArchive archive;
    archive.Open(source, 1, 1);
    RecordChunks(&archive, chunk, &records, &Record::Serialize);
    archive.Close(source);
    for (Record& record : records) names[IndexKey(record)] = &record;
}

int ItemDatabase::Load(const std::string& folder, const std::string& archive) {
    std::string path = folder + archive;
    dbfile::LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", path);
    std::uint32_t size;
    void* data = resource.Get()->MappedResource::GetFile("items.idb", &size);
    if (!data) return game::FatalError("Can't open items database\n");

    MemoryStream stream;
    dbfile::FillStream(stream, data, size);
    materials.Load(&stream, kMaterialsChunk);
    weapons.Load(&stream, kWeaponsChunk);
    armors.Load(&stream, kArmorsChunk);
    quickItems.Load(&stream, kQuickItemsChunk);
    questItems.Load(&stream, kQuestItemsChunk);
    loot.Load(&stream, kLootChunk);

    resource.Get()->MappedResource::CloseFile(data);
    resource.Get()->MappedResource::CloseResource();
    return 1;
}

} // namespace itemdb
