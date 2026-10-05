// Checks for item_lifetime.cpp.
// - CItem::Construct (0069FCA0) and CItemArmor::Construct (006A2F20): the original on a scratch copy
//   of the fresh memory (bytes neither writes stay equal), ours on the real object; whole object
//   compared. Nothing they release exists yet.
// - The other constructors start with a CALL within five bytes (no trampoline): ours only, the
//   fields CItem's constructor sets and the class's vtable and fields checked.
// - Create / Destroy (slots 2 / 3) release references: ours only, the state afterwards checked.
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {

void* s_unused;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t v;
    std::memcpy(&v, object, 4);
    return v;
}

std::uint32_t Bits(const void* field) {
    std::uint32_t u;
    std::memcpy(&u, field, 4);
    return u;
}

template <typename T>
T* ConstructBoth(const char* name, T*(__fastcall* original)(T*), T* self) {
    auto* scratch = static_cast<unsigned char*>(gamecrt::operator_new(sizeof(T)));
    std::memcpy(scratch, self, sizeof(T));
    T* expected = original(reinterpret_cast<T*>(scratch));
    T* actual = self->Construct();
    int diff = -1;
    for (std::size_t i = 0; i < sizeof(T) && diff < 0; ++i)
        if (scratch[i] != reinterpret_cast<const unsigned char*>(self)[i]) diff = static_cast<int>(i);
    ei::verify::Check(name, true, diff < 0 && expected == reinterpret_cast<T*>(scratch) && actual == self,
                      "object %p: first difference at +0x%X", self, diff);
    gamecrt::operator_delete(scratch);
    return actual;
}

// What CItem's constructor leaves.
bool BuiltItem(const CItem* item) {
    const std::uint32_t minusOne = 0xBF800000;
    bool ok = item->refCount_ == 0 && item->flags_ == 0 && item->id_ == -1 && item->type_ == 0 &&
              item->owner_ == nullptr && item->unknown_18 == 0 && item->unknown_1C == 0 && item->spell_ == nullptr &&
              item->unknown_48 == 1;
    const std::uint32_t* fields[] = {&item->unknown_20, &item->unknown_24, &item->unknown_34, &item->unknown_3C};
    for (const std::uint32_t* f : fields) ok = ok && *f == minusOne;
    ok = ok && Bits(&item->charge_) == minusOne && Bits(&item->chargeMax_) == minusOne &&
         Bits(&item->chargeRate_) == minusOne && Bits(&item->price_) == minusOne;
    return ok;
}

// 0069FCA0: PUSH -1; PUSH imm32 (7). 006A2F20: PUSH EBP; MOV EBP,ESP; SUB ESP,8; PUSH ESI (7).
CItem*(__fastcall* s_itemCtor)(CItem*);
CItemArmor*(__fastcall* s_armorCtor)(CItemArmor*);
CItem* __fastcall ItemCtorVerify(CItem* self) { return ConstructBoth("CItem::Construct", s_itemCtor, self); }
CItemArmor* __fastcall ArmorCtorVerify(CItemArmor* self) {
    return ConstructBoth("CItemArmor::Construct", s_armorCtor, self);
}

CItemWeapon* __fastcall WeaponCtorVerify(CItemWeapon* self) {
    CItemWeapon* result = self->Construct();
    bool extra = self->unknown_4C == 0 && Bits(&self->damage_) == 0xBF800000 &&
                 Bits(&self->damageSpread_) == 0xBF800000 && Bits(&self->unknown_58) == 0xBF800000;
    ei::verify::Check("CItemWeapon::Construct", true,
                      result == self && VtableOf(self) == 0x00743DFC && BuiltItem(self) && extra, "object %p", self);
    return result;
}
CItemQuick* __fastcall QuickCtorVerify(CItemQuick* self) {
    CItemQuick* result = self->Construct();
    ei::verify::Check("CItemQuick::Construct", true, result == self && VtableOf(self) == 0x00743EAC && BuiltItem(self),
                      "object %p", self);
    return result;
}
CItemQuest* __fastcall QuestCtorVerify(CItemQuest* self) {
    CItemQuest* result = self->Construct();
    ei::verify::Check("CItemQuest::Construct", true, result == self && VtableOf(self) == 0x00743F04 && BuiltItem(self),
                      "object %p", self);
    return result;
}
CItemLoot* __fastcall LootCtorVerify(CItemLoot* self) {
    CItemLoot* result = self->Construct();
    ei::verify::Check("CItemLoot::Construct", true, result == self && VtableOf(self) == 0x00743F5C && BuiltItem(self),
                      "object %p", self);
    return result;
}
CItemSpell* __fastcall SpellCtorVerify(CItemSpell* self) {
    CItemSpell* result = self->Construct();
    ei::verify::Check("CItemSpell::Construct", true, result == self && VtableOf(self) == 0x00743FB4 && BuiltItem(self),
                      "object %p", self);
    return result;
}

int __fastcall CreateVerify(CItem* self) {
    int result = self->CreateItem();
    ei::verify::Check("CItem::CreateItem", true, result == 1 && self->unknown_48 == 1 && self->spell_ == nullptr,
                      "item %p", self);
    return result;
}
void __fastcall DestroyVerify(CItem* self) {
    self->DestroyItem();
    ei::verify::Check("CItem::DestroyItem", true, self->spell_ == nullptr && self->unknown_1C == 0, "item %p", self);
}

} // namespace

EI_VERIFY(0x0069FCA0, ItemCtorVerify, 7, s_itemCtor);
EI_VERIFY(0x006A2F20, ArmorCtorVerify, 7, s_armorCtor);
EI_VERIFY(0x006A0540, WeaponCtorVerify, 0, s_unused);
EI_VERIFY(0x006A3AD0, QuickCtorVerify, 0, s_unused);
EI_VERIFY(0x006A43A0, QuestCtorVerify, 0, s_unused);
EI_VERIFY(0x006A49A0, LootCtorVerify, 0, s_unused);
EI_VERIFY(0x006A56E0, SpellCtorVerify, 0, s_unused);
EI_VERIFY(0x0069FE20, CreateVerify, 0, s_unused);
EI_VERIFY(0x0069FE50, DestroyVerify, 0, s_unused);
