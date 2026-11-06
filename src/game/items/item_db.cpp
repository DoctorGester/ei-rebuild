// The item database: its records' Serialize, the tables' load and ItemDatabase::Load ("items.idb", at startup and
// at every switch of the resource database). The spell database is spell_db.cpp. See item_db.h and
// docs/notes/items_spells.md ("Databases and naming").
#include "game/items/item_db.h"

#include "game/data/chunk_archive.h"
#include "game/core/core_streams.h"
#include "game/data/database_file.h"
#include "game/units/unit_db.h"   // InlineStrings

#include "lib/crt.h"
#include "lib/text_case.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

// The database of the items: built empty by the C++ runtime, loaded at start.
itemdb::ItemDatabase g_itemDatabase;

namespace {

// The chunks of items.idb.
constexpr int kMaterialsChunk = 1;
constexpr int kWeaponsChunk = 2;
constexpr int kArmorsChunk = 3;
constexpr int kQuickItemsChunk = 4;
constexpr int kQuestItemsChunk = 5;
constexpr int kLootChunk = 6;

// An armour's absorption row as items.idb holds it (fields 0x15 and 0x16): the factor, the absorption per damage type
// and the flags.
struct AbsorbRow {
    float factor = 0.0f;
    std::array<float, kDamageTypeCount> absorb{};
    std::uint32_t flags = 0;
};
static_assert(sizeof(AbsorbRow) == 0x24);
constexpr bool NoPadding(const AbsorbRow*) {
    return lib::FieldsFill<&AbsorbRow::factor, &AbsorbRow::absorb, &AbsorbRow::flags>();
}

void AbsorbRowField(ChunkArchive* ar, int id, float& factor, std::array<float, kDamageTypeCount>& absorb,
                    std::uint32_t& flags) {
    AbsorbRow row{factor, absorb, flags};
    ar->Field(id, &row);
    factor = row.factor;
    absorb = row.absorb;
    flags = row.flags;
}

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

bool ItemRecordHead::HasMaterial() const { return text::CompareIgnoringCase(materialClass, kNoMaterialClass) != 0; }

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
    ar->Field(0x1A, &damageTypes);
    ar->Field(0x1B, unreadFields + 8, 3);
    // Field 0x1C: the two skill bonus bytes, then six nothing reads.
    std::array<std::uint8_t, 8> skillField{};
    std::copy_n(skillBonus, 2, skillField.begin());
    std::copy_n(unreadFields + 11, 6, skillField.begin() + 2);
    ar->Field(0x1C, &skillField);
    std::copy_n(skillField.begin(), 2, skillBonus);
    std::copy_n(skillField.begin() + 2, 6, unreadFields + 11);
    ar->Field(0x1D, &attackBonus);
    ar->Field(0x1E, &defenceBonus);
}

void ArmorRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    AbsorbRowField(ar, 0x15, firstFactor, absorbFirst, firstFlags);
    AbsorbRowField(ar, 0x16, othersFactor, absorbOthers, othersFlags);
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
void RecordTable<Record>::Load(ByteStream* source, int chunk) {
    ChunkArchive archive;
    archive.Open(source, 1, 1);
    RecordChunks(&archive, chunk, &records, &Record::Serialize);
    archive.Close(source);
    for (Record& record : records) names[std::string(IndexKey(record))] = &record;
}
// SpellDatabase::Load (spell_db.cpp) loads these two.
template void SpellTable::Load(ByteStream*, int);
template void SpellModifierTable::Load(ByteStream*, int);

bool ItemDatabase::Load(const std::string& folder, const std::string& archive) {
    std::string path = folder + archive;
    MappedResource resource;
    resource.OpenResource(path);
    std::uint32_t size;
    void* data = resource.GetFile("items.idb", &size);
    if (!data) game::FatalExit("Can't open items database\n");

    MemoryStream stream;
    stream.Fill(data, size);
    materials.Load(&stream, kMaterialsChunk);
    weapons.Load(&stream, kWeaponsChunk);
    armors.Load(&stream, kArmorsChunk);
    quickItems.Load(&stream, kQuickItemsChunk);
    questItems.Load(&stream, kQuestItemsChunk);
    loot.Load(&stream, kLootChunk);

    resource.CloseFile(data);
    resource.CloseResource();
    return true;
}

} // namespace itemdb
