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
    ar->Field(0x1C, skillBonus, 8);   // +0x7B..+0x82
    ar->Field(0x1D, &attackBonus);
    ar->Field(0x1E, &defenceBonus);
}

void ArmorRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 0x24);           // +0x6C..+0x8F: the factor (category), absorbFirst and the flags
    ar->Field(0x16, &othersFactor, 0x24);       // +0x90..+0xB3: the factor, absorbOthers and the flags
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
    // The list is emptied by the element destructors; the result is the same.
    InlineStrings(ar, 0x16, &zones);
}

void LootItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category);
    ar->Field(0x16, &recordType);
    ar->Field(0x17, &index);
}

} // namespace itemdb
