// The item records' Serialize (A-H23): what the item database's vector serialisers
// (item_db_load.cpp) run for every material, weapon, armour, quick, quest and loot record. Fields
// by id through the chunk archive; see item_db.h.
#include "item_db.h"

#include "chunk_archive.h"
#include "unit_db.h"   // CStringVector, InlineStrings

#include "ei/hook.h"

namespace itemdb {

namespace {
// The records keep their strings as CString members declared as character pointers (the
// material head) or as an STLport vector<CString> spelled as three pointers (the quest item).
mfc::CString* AsCString(const char** text) { return reinterpret_cast<mfc::CString*>(text); }
} // namespace

void MaterialRecordHead::Serialize(ChunkArchive* ar) {
    ar->CStringField(0, AsCString(&key));
    ar->CStringField(1, AsCString(&soundName));
    ar->CStringField(2, AsCString(&name));
    ar->Field(3, &unknown_0C, 4);
    ar->Field(4, &quality, 4);
    ar->Field(5, &unknown_14, 4);
    ar->Field(6, &magicPower, 4);
    ar->Field(7, &magicLevel, 4);
    for (int i = 0; i < 3; ++i) ar->Field(8 + i, &unknown_20[i], 4);
    ar->Field(11, unknown_2C, sizeof(unknown_2C));
    ar->Field(12, &unknown_48, 4);
    ar->Field(13, &unknown_4C, 4);
}
EI_REPLACE_METHOD(0x0069F620, MaterialRecordHead::Serialize);

void ItemRecordHead::SerializeHead(ChunkArchive* ar) {
    ar->CStringField(0, &text04);
    ar->CStringField(1, &text08);
    ar->Field(2, &type, 4);
    ar->CStringField(3, &text10);
    ar->Field(4, &unknown_14, 4);
    ar->Field(5, &number, 4);
    ar->Field(6, &variant, 4);
    ar->Field(7, &price, 4);
    ar->Field(8, &value38, 4);
    ar->Field(9, &value3C, 4);
    ar->Field(10, &charge, 4);
    ar->Field(11, &value44, 4);
    ar->Field(12, &value48, 4);
    ar->Field(13, &unknown_4C, 4);
    ar->Field(14, &unknown_64, 4);
    ar->Field(15, &unknown_68, 1);
}
EI_REPLACE_METHOD(0x0069F700, ItemRecordHead::SerializeHead);

void WeaponRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    ar->Field(0x16, &unknown_70[0], 8);
    ar->Field(0x17, &value8C, 4);
    ar->Field(0x18, &damage, 4);
    ar->Field(0x19, &damageSpread, 4);
    ar->Field(0x1A, damageTypes, sizeof(damageTypes));
    ar->Field(0x1B, &unknown_70[8], 3);
    ar->Field(0x1C, skillBonus, 8);   // +0x7B..+0x82
    ar->Field(0x1D, &attackBonus, 4);
    ar->Field(0x1E, &defenceBonus, 4);
}
EI_REPLACE_METHOD(0x0069F800, WeaponRecord::Serialize);

void ArmorRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 0x24);           // +0x6C..+0x8F: the category, absorbFirst and a dword
    ar->Field(0x16, &unknown_8C[4], 0x24);      // +0x90..+0xB3: a dword, absorbOthers and a dword
    ar->Field(0x17, &showsWounds, 1);
    ar->Field(0x18, &layer, 4);
    ar->Field(0x19, &unknown_BC[0], 3);
    ar->Field(0x1A, &unknown_BC[3], 8);
}
EI_REPLACE_METHOD(0x0069F8C0, ArmorRecord::Serialize);

void QuickItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    ar->Field(0x16, &value70, 4);
    ar->Field(0x17, &unknown_74[0], 4);
    ar->Field(0x18, &unknown_74[4], 4);
    ar->CStringField(0x19, &text7C);
    ar->Field(0x1A, attributeBonus, 3);
    ar->Field(0x1B, &unknown_83[0], 8);
}
EI_REPLACE_METHOD(0x0069F940, QuickItemRecord::Serialize);

void QuestItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    // The original erases the list through 0051F970 and the element destructors instead of
    // 0052E010; the result is the same.
    InlineStrings(ar, 0x16, reinterpret_cast<CStringVector*>(&zonesBegin));
}
EI_REPLACE_METHOD(0x0069F9C0, QuestItemRecord::Serialize);

void LootItemRecord::Serialize(ChunkArchive* ar) {
    SerializeHead(ar);
    ar->Field(0x15, &category, 4);
    ar->Field(0x16, &recordType, 4);
    ar->Field(0x17, &index, 4);
}
EI_REPLACE_METHOD(0x0069FA90, LootItemRecord::Serialize);

} // namespace itemdb
