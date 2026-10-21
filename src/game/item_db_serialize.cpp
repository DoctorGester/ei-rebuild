// The item records' Serialize (A-H23): what the item database's vector serialisers
// (item_db_load.cpp) run for every material, weapon, armour, quick, quest and loot record. Fields
// by id through the chunk archive; see item_db.h.
#include "item_db.h"

#include "chunk_archive.h"
#include "unit_db.h"   // CStringVector, InlineStrings

#include "ei/hook.h"

namespace itemdb {

void MaterialRecordHead::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, &key);
    ar->CStringField(1, &soundName);
    ar->CStringField(2, &name);
    ar->Field(3, &number, 4);
    ar->Field(4, &unitPrice, 4);
    ar->Field(5, &factor38, 4);
    ar->Field(6, &magicPower, 4);
    ar->Field(7, &magicLevel, 4);
    ar->Field(8, &conditionFactor, 4);
    ar->Field(9, &field9, 4);
    ar->Field(10, &damageFactor, 4);
    ar->Field(11, absorbFactors, sizeof(absorbFactors));
    ar->Field(12, &armourFlags, 4);
    ar->Field(13, &classMask, 4);
}
EI_REWRITTEN(0x0069F620, MaterialRecordHead::Serialize);

void ItemRecordHead::SerializeHead(ChunkArchive* ar) {
    ar->CStringField(0, &text04);
    ar->CStringField(1, &text08);
    ar->Field(2, &type, 4);
    ar->CStringField(3, &materialClass);
    ar->Field(4, &field4, 4);
    ar->Field(5, &number, 4);
    ar->Field(6, &variant, 4);
    ar->Field(7, &price, 4);
    ar->Field(8, &value38, 4);
    ar->Field(9, &value3C, 4);
    ar->Field(10, &charge, 4);
    ar->Field(11, &magicLevel, 4);
    ar->Field(12, &condition, 4);
    ar->Field(13, &materialUnits, 4);
    ar->Field(14, &classMask, 4);
    ar->Field(15, &field15, 1);
}
EI_REWRITTEN(0x0069F700, ItemRecordHead::SerializeHead);

void WeaponRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    ar->Field(0x16, field16, 8);
    ar->Field(0x17, &reach, 4);
    ar->Field(0x18, &damage, 4);
    ar->Field(0x19, &damageSpread, 4);
    ar->Field(0x1A, damageTypes, sizeof(damageTypes));
    ar->Field(0x1B, field1B, 3);
    ar->Field(0x1C, skillBonus, 8);   // +0x7B..+0x82
    ar->Field(0x1D, &attackBonus, 4);
    ar->Field(0x1E, &defenceBonus, 4);
}
EI_REPLACE_METHOD(0x0069F800, WeaponRecord::Serialize);

void ArmorRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 0x24);           // +0x6C..+0x8F: the factor (category), absorbFirst and the flags
    ar->Field(0x16, &othersFactor, 0x24);       // +0x90..+0xB3: the factor, absorbOthers and the flags
    ar->Field(0x17, &showsWounds, 1);
    ar->Field(0x18, &layer, 4);
    ar->Field(0x19, field19, 3);
    ar->Field(0x1A, field1A, 8);
}
EI_REPLACE_METHOD(0x0069F8C0, ArmorRecord::Serialize);

void QuickItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    ar->Field(0x16, &value70, 4);
    ar->Field(0x17, field17, 4);
    ar->Field(0x18, field18, 4);
    ar->CStringField(0x19, &text7C);
    ar->Field(0x1A, attributeBonus, 3);
    ar->Field(0x1B, field1B, 8);
}
EI_REWRITTEN(0x0069F940, QuickItemRecord::Serialize);

void QuestItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    // The original erases the list through 0051F970 and the element destructors instead of
    // 0052E010; the result is the same.
    InlineStrings(ar, 0x16, reinterpret_cast<CStringVector*>(&zonesBegin));
}
EI_REWRITTEN(0x0069F9C0, QuestItemRecord::Serialize);

void LootItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    ar->Field(0x16, &recordType, 4);
    ar->Field(0x17, &index, 4);
}
EI_REPLACE_METHOD(0x0069FA90, LootItemRecord::Serialize);

} // namespace itemdb
