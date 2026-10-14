// The items' constructor and destructor bodies, slot 0 and their Create / Destroy slots (2 / 3).
// See item.h and docs/notes/items_spells.md ("Item rules").
#include "item.h"

#include "core_refcount.h"
#include "../lib/crt.h"

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
    link_ = 0;
    spell_ = nullptr;
    SetVtable(this, Vtable());
    SetMinusOne(condition_);
    SetMinusOne(conditionMax_);
    SetMinusOne(charge_);
    SetMinusOne(chargeMax_);
    SetMinusOne(chargeRate_);
    SetMinusOne(weight_);
    SetMinusOne(price_);
    SetMinusOne(unknown_3C);
    count_ = 1;
    DropSpell(spell_);
    return this;
}
EI_REPLACE_METHOD(0x0069FCA0, CItem::Construct);

int CItem::CreateItem() {
    count_ = 1;
    DropSpell(spell_);
    return 1;
}
EI_REPLACE_METHOD(0x0069FE20, CItem::CreateItem);

void CItem::DestroyItem() {
    DropSpell(spell_);
    CRefCounted* link = Link(link_);
    link_ = 0;
    if (link != nullptr && --link->refs_ == 0) delete link;
}
EI_REPLACE_METHOD(0x0069FE50, CItem::DestroyItem);

void CItem::DestructBody() {
    SetVtable(this, Vtable());
    DestroyItem();
    // The members' destructors: the spell released and the link's count dropped, without clearing.
    if (spell_ != nullptr) static_cast<CEngineObject*>(spell_)->Release();
    CRefCounted* link = Link(link_);
    if (link != nullptr && --link->refs_ == 0) delete link;
    DestructBase();
}
EI_REPLACE_METHOD(0x0069FD70, CItem::DestructBody);
EI_REPLACE_METHOD(0x006A05B0, CItem::DestructBody);
EI_REPLACE_METHOD(0x006A2FC0, CItem::DestructBody);
EI_REPLACE_METHOD(0x006A3B30, CItem::DestructBody);
EI_REPLACE_METHOD(0x006A4420, CItem::DestructBody);
EI_REPLACE_METHOD(0x006A4A00, CItem::DestructBody);
EI_REPLACE_METHOD(0x006A5740, CItem::DestructBody);

CItem* CItem::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0069FD30, CItem::DeletingDestruct);
EI_REPLACE_METHOD(0x006A0590, CItem::DeletingDestruct);
EI_REPLACE_METHOD(0x006A2FA0, CItem::DeletingDestruct);
EI_REPLACE_METHOD(0x006A3B10, CItem::DeletingDestruct);
EI_REPLACE_METHOD(0x006A4400, CItem::DeletingDestruct);
EI_REPLACE_METHOD(0x006A49E0, CItem::DeletingDestruct);
EI_REPLACE_METHOD(0x006A5720, CItem::DeletingDestruct);

CItemWeapon* CItemWeapon::Construct() {
    CItem::Construct();
    SetVtable(this, WeaponVtable());
    SetMinusOne(damage_);
    SetMinusOne(damageSpread_);
    SetMinusOne(reach_);
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

// --- The item manager's Create and factory (A-H25) -------------------------------------------------

BOOL __fastcall ItemManagerCreate(CItemManager* self) { return self->CBaseManager::Create() != 0 ? 1 : 0; }
EI_REPLACE(0x006AC780, ItemManagerCreate);

static_assert(sizeof(CItemWeapon) == 0x5C && sizeof(CItemArmor) == 0x8C && sizeof(CItemLoot) == 0x58);
static_assert(sizeof(CItemQuest) == 0x50 && sizeof(CItemSpell) == 0x50);

namespace {
template <typename T>
CEngineObject* NewItem() {
    void* memory = gamecrt::operator_new(sizeof(T));
    return memory != nullptr ? static_cast<T*>(memory)->Construct() : nullptr;
}
} // namespace

CEngineObject* __fastcall ItemNewObject(CBaseManager*, void*, int kind) {
    CEngineObject* object;
    switch (kind) {
    case 0x3004: object = NewItem<CItemWeapon>(); break;
    case 0x3005: object = NewItem<CItemArmor>(); break;
    case 0x3006: object = NewItem<CItemQuick>(); break;
    case 0x3007: object = NewItem<CItemLoot>(); break;
    case 0x3008: object = NewItem<CItemSpell>(); break;
    case 0x3009: object = NewItem<CItemQuest>(); break;
    default: return nullptr;
    }
    if (object != nullptr) object->type_ = kind;
    return object;
}
EI_REPLACE(0x006AC810, ItemNewObject);
