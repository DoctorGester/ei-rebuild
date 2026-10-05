// Checks for item_slots.cpp: the items' slot 14 (IsSameAs) side by side, result compared; slot 13
// (InitFromRecord) with the original on a scratch copy of the item and ours on the real one, the
// whole object compared.
#include "verify.h"

#include "../../src/game/item.h"

#include <cstring>

namespace {

template <typename T>
BOOL SameBoth(const char* name, BOOL(__fastcall* original)(T*, void*, T*), T* self, T* other) {
    BOOL expected = original(self, nullptr, other);
    BOOL actual = self->IsSameAs(other);
    ei::verify::Check(name, expected, actual, "items %p / %p", self, other);
    return expected;
}

template <typename T>
void InitBoth(const char* name, void(__fastcall* original)(T*, void*, const char*), T* self, const char* record) {
    alignas(4) unsigned char scratch[sizeof(T)];
    std::memcpy(scratch, self, sizeof(T));
    original(reinterpret_cast<T*>(scratch), nullptr, record);
    self->InitFromRecord(record);
    int diff = -1;
    for (std::size_t i = 0; i < sizeof(T) && diff < 0; ++i)
        if (scratch[i] != reinterpret_cast<const unsigned char*>(self)[i]) diff = static_cast<int>(i);
    ei::verify::Check(name, true, diff < 0, "item %p, record %p: first difference at +0x%X", self, record, diff);
}

// 0069FE90: MOV EDX,[ESP+4] (4); PUSH ESI (1).
BOOL(__fastcall* s_itemSame)(CItem*, void*, CItem*);
BOOL __fastcall ItemSameVerify(CItem* self, void*, CItem* other) { return SameBoth("CItem::IsSameAs", s_itemSame, self, other); }

// 006A30D0: PUSH EBX; PUSH EBP; MOV EBP,[ESP+0Ch] (6). 006A3C20 / 006A4510 / 006A4B00: PUSH ESI;
// PUSH EDI; MOV EDI,[ESP+0Ch] (6).
BOOL(__fastcall* s_armorSame)(CItemArmor*, void*, CItemArmor*);
BOOL(__fastcall* s_quickSame)(CItemQuick*, void*, CItemQuick*);
BOOL(__fastcall* s_questSame)(CItemQuest*, void*, CItemQuest*);
BOOL(__fastcall* s_lootSame)(CItemLoot*, void*, CItemLoot*);
BOOL __fastcall ArmorSameVerify(CItemArmor* self, void*, CItemArmor* other) {
    return SameBoth("CItemArmor::IsSameAs", s_armorSame, self, other);
}
BOOL __fastcall QuickSameVerify(CItemQuick* self, void*, CItemQuick* other) {
    return SameBoth("CItemQuick::IsSameAs", s_quickSame, self, other);
}
BOOL __fastcall QuestSameVerify(CItemQuest* self, void*, CItemQuest* other) {
    return SameBoth("CItemQuest::IsSameAs", s_questSame, self, other);
}
BOOL __fastcall LootSameVerify(CItemLoot* self, void*, CItemLoot* other) {
    return SameBoth("CItemLoot::IsSameAs", s_lootSame, self, other);
}

// 006A0660 / 006A3BE0 / 006A44D0 / 006A4AB0: MOV EAX,[ESP+4] (4); FLD [EAX+48h] (3).
// 006A3070: MOV EDX,[ESP+4] (4); MOV EAX,ECX (2).
void(__fastcall* s_weaponInit)(CItemWeapon*, void*, const char*);
void(__fastcall* s_armorInit)(CItemArmor*, void*, const char*);
void(__fastcall* s_quickInit)(CItemQuick*, void*, const char*);
void(__fastcall* s_questInit)(CItemQuest*, void*, const char*);
void(__fastcall* s_lootInit)(CItemLoot*, void*, const char*);
void __fastcall WeaponInitVerify(CItemWeapon* self, void*, const char* record) {
    InitBoth("CItemWeapon::InitFromRecord", s_weaponInit, self, record);
}
void __fastcall ArmorInitVerify(CItemArmor* self, void*, const char* record) {
    InitBoth("CItemArmor::InitFromRecord", s_armorInit, self, record);
}
void __fastcall QuickInitVerify(CItemQuick* self, void*, const char* record) {
    InitBoth("CItemQuick::InitFromRecord", s_quickInit, self, record);
}
void __fastcall QuestInitVerify(CItemQuest* self, void*, const char* record) {
    InitBoth("CItemQuest::InitFromRecord", s_questInit, self, record);
}
void __fastcall LootInitVerify(CItemLoot* self, void*, const char* record) {
    InitBoth("CItemLoot::InitFromRecord", s_lootInit, self, record);
}

} // namespace

EI_VERIFY(0x0069FE90, ItemSameVerify, 5, s_itemSame);
EI_VERIFY(0x006A30D0, ArmorSameVerify, 6, s_armorSame);
EI_VERIFY(0x006A3C20, QuickSameVerify, 6, s_quickSame);
EI_VERIFY(0x006A4510, QuestSameVerify, 6, s_questSame);
EI_VERIFY(0x006A4B00, LootSameVerify, 6, s_lootSame);

EI_VERIFY(0x006A0660, WeaponInitVerify, 7, s_weaponInit);
EI_VERIFY(0x006A3070, ArmorInitVerify, 6, s_armorInit);
EI_VERIFY(0x006A3BE0, QuickInitVerify, 7, s_quickInit);
EI_VERIFY(0x006A44D0, QuestInitVerify, 7, s_questInit);
EI_VERIFY(0x006A4AB0, LootInitVerify, 7, s_lootInit);
