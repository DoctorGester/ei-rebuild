// The item records' Serialize: what the item database's vector serialisers (item_db_load.cpp) run for every
// material, weapon, armour, quick, quest and loot record. Fields by id through the chunk archive; see item_db.h.
#include "item_db.h"

#include "chunk_archive.h"
#include "unit_db.h"   // InlineStrings

namespace itemdb {

void MaterialRecordHead::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &key);
    ar->StringField(1, &soundName);
    ar->StringField(2, &name);
    ar->Field(3, &number, 4);
    ar->Field(4, &unitPrice, 4);
    ar->Field(5, &weightFactor, 4);
    ar->Field(6, &magicPower, 4);
    ar->Field(7, &magicLevel, 4);
    ar->Field(8, &conditionFactor, 4);
    ar->Field(9, &unreadField, 4);
    ar->Field(10, &damageFactor, 4);
    ar->Field(11, absorbFactors, sizeof(absorbFactors));
    ar->Field(12, &armourFlags, 4);
    ar->Field(13, &classMask, 4);
}

void ItemRecordHead::SerializeHead(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &family);
    ar->Field(2, &type, 4);
    ar->StringField(3, &materialClass);
    ar->Field(4, &unreadHeadField, 4);
    ar->Field(5, &number, 4);
    ar->Field(6, &variant, 4);
    ar->Field(7, &price, 4);
    ar->Field(8, &weight, 4);
    ar->Field(9, &unreadValue, 4);
    ar->Field(10, &charge, 4);
    ar->Field(11, &magicLevel, 4);
    ar->Field(12, &condition, 4);
    ar->Field(13, &materialUnits, 4);
    ar->Field(14, &classMask, 4);
    ar->Field(15, &buildable, 1);
}

void WeaponRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    ar->Field(0x16, unreadFields, 8);
    ar->Field(0x17, &reach, 4);
    ar->Field(0x18, &damage, 4);
    ar->Field(0x19, &damageSpread, 4);
    ar->Field(0x1A, damageTypes, sizeof(damageTypes));
    ar->Field(0x1B, unreadFields + 8, 3);
    ar->Field(0x1C, skillBonus, 8);   // +0x7B..+0x82
    ar->Field(0x1D, &attackBonus, 4);
    ar->Field(0x1E, &defenceBonus, 4);
}

void ArmorRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 0x24);           // +0x6C..+0x8F: the factor (category), absorbFirst and the flags
    ar->Field(0x16, &othersFactor, 0x24);       // +0x90..+0xB3: the factor, absorbOthers and the flags
    ar->Field(0x17, &showsWounds, 1);
    ar->Field(0x18, &layer, 4);
    ar->Field(0x19, unreadFields, 3);
    ar->Field(0x1A, unreadFields + 3, 8);
}

void QuickItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    ar->Field(0x16, &actionValue, 4);
    ar->Field(0x17, unreadFields, 4);
    ar->Field(0x18, unreadFields + 4, 4);
    ar->StringField(0x19, &spellText);
    ar->Field(0x1A, attributeBonus, 3);
    ar->Field(0x1B, unreadFields + 8, 8);
}

void QuestItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    // The list is emptied by the element destructors; the result is the same.
    InlineStrings(ar, 0x16, &zones);
}

void LootItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    ar->Field(0x16, &recordType, 4);
    ar->Field(0x17, &index, 4);
}

} // namespace itemdb
