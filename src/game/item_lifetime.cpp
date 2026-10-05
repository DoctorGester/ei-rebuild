// The items' constructor bodies and their Create / Destroy slots (2 / 3). See item.h and
// docs/notes/items_spells.md ("Item rules").
#include "item.h"

#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstring>

using ei::SetVtable;

namespace {

// The subclasses' vtables.
std::uintptr_t WeaponVtable() { return static_cast<std::uintptr_t>(0x00743DFC); }
std::uintptr_t ArmorVtable() { return static_cast<std::uintptr_t>(0x00743E54); }
std::uintptr_t QuickVtable() { return static_cast<std::uintptr_t>(0x00743EAC); }
std::uintptr_t QuestVtable() { return static_cast<std::uintptr_t>(0x00743F04); }
std::uintptr_t LootVtable() { return static_cast<std::uintptr_t>(0x00743F5C); }
std::uintptr_t SpellVtable() { return static_cast<std::uintptr_t>(0x00743FB4); }

constexpr std::uint32_t kMinusOne = 0xBF800000;   // -1.0f, stored as a dword

template <typename T>
void SetMinusOne(T& field) {
    static_assert(sizeof(T) == 4);
    std::memcpy(&field, &kMinusOne, 4);
}

// The item's link (+0x1C): a CRefCounted with a 16-bit count, kept as a raw dword.
CRefCounted* Link(std::uint32_t value) { return reinterpret_cast<CRefCounted*>(static_cast<std::uintptr_t>(value)); }

// The spell let go through its slot 4 (Release) after the pointer is cleared.
void DropSpell(void*& spell) {
    auto* old = static_cast<CEngineObject*>(spell);
    spell = nullptr;
    if (old != nullptr) old->Release();
}

} // namespace

std::uintptr_t CItem::Vtable() { return static_cast<std::uintptr_t>(0x00743DA4); }

CItem* CItem::Construct() {
    ClearFields();
    unknown_1C = 0;
    spell_ = nullptr;
    SetVtable(this, Vtable());
    SetMinusOne(unknown_20);
    SetMinusOne(unknown_24);
    SetMinusOne(charge_);
    SetMinusOne(chargeMax_);
    SetMinusOne(chargeRate_);
    SetMinusOne(unknown_34);
    SetMinusOne(price_);
    SetMinusOne(unknown_3C);
    unknown_48 = 1;
    DropSpell(spell_);
    return this;
}
EI_REPLACE_METHOD(0x0069FCA0, CItem::Construct);

int CItem::CreateItem() {
    unknown_48 = 1;
    DropSpell(spell_);
    return 1;
}
EI_REPLACE_METHOD(0x0069FE20, CItem::CreateItem);

void CItem::DestroyItem() {
    DropSpell(spell_);
    CRefCounted* link = Link(unknown_1C);
    unknown_1C = 0;
    if (link != nullptr && --link->refs_ == 0) delete link;
}
EI_REPLACE_METHOD(0x0069FE50, CItem::DestroyItem);

CItemWeapon* CItemWeapon::Construct() {
    CItem::Construct();
    SetVtable(this, WeaponVtable());
    SetMinusOne(damage_);
    SetMinusOne(damageSpread_);
    SetMinusOne(unknown_58);
    unknown_4C = 0;
    return this;
}
EI_REPLACE_METHOD(0x006A0540, CItemWeapon::Construct);

CItemArmor* CItemArmor::Construct() {
    CItem::Construct();
    SetVtable(this, ArmorVtable());
    std::memset(absorb_, 0, sizeof(absorb_));
    layerMask_[1] = 0;
    layerMask_[0] = 0;
    return this;
}
EI_REPLACE_METHOD(0x006A2F20, CItemArmor::Construct);

CItemQuick* CItemQuick::Construct() {
    CItem::Construct();
    SetVtable(this, QuickVtable());
    return this;
}
EI_REPLACE_METHOD(0x006A3AD0, CItemQuick::Construct);

CItemQuest* CItemQuest::Construct() {
    CItem::Construct();
    SetVtable(this, QuestVtable());
    return this;
}
EI_REPLACE_METHOD(0x006A43A0, CItemQuest::Construct);

CItemLoot* CItemLoot::Construct() {
    CItem::Construct();
    SetVtable(this, LootVtable());
    return this;
}
EI_REPLACE_METHOD(0x006A49A0, CItemLoot::Construct);

CItemSpell* CItemSpell::Construct() {
    CItem::Construct();
    SetVtable(this, SpellVtable());
    return this;
}
EI_REPLACE_METHOD(0x006A56E0, CItemSpell::Construct);
