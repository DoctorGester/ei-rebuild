// Checks for item_lifetime.cpp.
// - CItem::Construct (0069FCA0) and CItemArmor::Construct (006A2F20): the original on a scratch copy
//   of the fresh memory (bytes neither writes stay equal), ours on the real object; whole object
//   compared. Nothing they release exists yet.
// - The other constructors start with a CALL within five bytes (no trampoline): ours only, the
//   fields CItem's constructor sets and the class's vtable and fields checked.
// - Create / Destroy (slots 2 / 3) release references: ours only, the state afterwards checked.
// - Destructor bodies and slot 0 (every class): ours only, the object checked afterwards (slot 0
//   runs without the free, which the wrapper then does). Reached when the item manager deletes
//   items (its Destroy and FreeSpares: loading a save in world_load / ui_saveload, the game's end).
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
              item->owner_ == nullptr && item->unknown_18 == 0 && item->link_ == 0 && item->spell_ == nullptr &&
              item->count_ == 1;
    const std::uint32_t* fields[] = {&item->condition_, &item->conditionMax_, &item->weight_, &item->unknown_3C};
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
                 Bits(&self->damageSpread_) == 0xBF800000 && Bits(&self->reach_) == 0xBF800000;
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
    ei::verify::Check("CItem::CreateItem", true, result == 1 && self->count_ == 1 && self->spell_ == nullptr,
                      "item %p", self);
    return result;
}
void __fastcall DestroyVerify(CItem* self) {
    self->DestroyItem();
    ei::verify::Check("CItem::DestroyItem", true, self->spell_ == nullptr && self->link_ == 0, "item %p", self);
}

// What the destructor body leaves: CEngineObject's vtable, no owner, spell or link.
bool Destructed(const CItem* item) {
    return VtableOf(item) == CEngineObject::kVtableAddress && item->owner_ == nullptr && item->spell_ == nullptr &&
           item->link_ == 0;
}
void __fastcall DestructVerify(CItem* self) {
    self->DestructBody();
    ei::verify::Check("CItem::DestructBody", true, Destructed(self), "item %p", self);
}
// Slot 0 runs without the free (bit 0 cleared) so the object can be checked; the wrapper frees it.
CItem* __fastcall DeletingVerify(CItem* self, void*, unsigned flags) {
    CItem* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CItem::DeletingDestruct", true, result == self && Destructed(self), "item %p flags %u", self, flags);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
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
EI_VERIFY(0x0069FD70, DestructVerify, 0, s_unused);
EI_VERIFY(0x006A05B0, DestructVerify, 0, s_unused);
EI_VERIFY(0x006A2FC0, DestructVerify, 0, s_unused);
EI_VERIFY(0x006A3B30, DestructVerify, 0, s_unused);
EI_VERIFY(0x006A4420, DestructVerify, 0, s_unused);
EI_VERIFY(0x006A4A00, DestructVerify, 0, s_unused);
EI_VERIFY(0x006A5740, DestructVerify, 0, s_unused);
EI_VERIFY(0x0069FD30, DeletingVerify, 0, s_unused);
EI_VERIFY(0x006A0590, DeletingVerify, 0, s_unused);
EI_VERIFY(0x006A2FA0, DeletingVerify, 0, s_unused);
EI_VERIFY(0x006A3B10, DeletingVerify, 0, s_unused);
EI_VERIFY(0x006A4400, DeletingVerify, 0, s_unused);
EI_VERIFY(0x006A49E0, DeletingVerify, 0, s_unused);
EI_VERIFY(0x006A5720, DeletingVerify, 0, s_unused);

// A-H25. The item manager's Create (slot 2: a world's or party's item manager set up) and its
// factory (slot 14: every item made that no spare covers). Ours only: Create empties the tables,
// the factory allocates; a new item reports the kind's class and carries the kind in type_.
namespace {
BOOL __fastcall ManagerCreateVerify(CItemManager* self) {
    BOOL result = ItemManagerCreate(self);
    ei::verify::Check("ItemManagerCreate", true, result == 0 || result == 1, "manager %p -> %d", static_cast<void*>(self),
                      result);
    return result;
}

int ClassOfKind(int kind) {
    switch (kind) {
    case 0x3004: return 0x11;
    case 0x3005: return 0x12;
    case 0x3006: return 0x13;
    case 0x3007: return 0x15;
    case 0x3008: return 0x16;
    case 0x3009: return 0x14;
    default: return -1;
    }
}

CEngineObject* __fastcall NewObjectVerify(CBaseManager* manager, void*, int kind) {
    CEngineObject* object = ItemNewObject(manager, nullptr, kind);
    const int expectedClass = ClassOfKind(kind);
    const bool ok = expectedClass < 0 ? object == nullptr
                                      : object == nullptr || (object->type_ == kind && object->GetClassId() == expectedClass);
    ei::verify::Check("ItemNewObject", true, ok, "kind %X object %p", kind, static_cast<void*>(object));
    return object;
}
} // namespace
EI_VERIFY(0x006AC780, ManagerCreateVerify, 0, s_unused);
EI_VERIFY(0x006AC810, NewObjectVerify, 0, s_unused);
