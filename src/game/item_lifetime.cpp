// The items' constructors and destructors, their Create / Destroy slots (2 / 3) and class names
// (11 / 12). See item.h and docs/notes/items_spells.md ("Item rules").
#include "item.h"

#include "core_refcount.h"
#include "spell.h"

#include <cstring>

#undef GetClassName   // CEngineObject::GetClassName

namespace {

constexpr std::uint32_t kMinusOne = 0xBF800000;   // -1.0f, stored as a dword

template <typename T>
void SetMinusOne(T& field) {
    static_assert(sizeof(T) == 4);
    std::memcpy(&field, &kMinusOne, 4);
}

CRefCounted* Link(CRefObject* value) { return value; }

void DropSpell(CSpell*& spell) {
    CSpell* old = spell;
    spell = nullptr;
    if (old != nullptr) old->Release();
}

} // namespace

CItem::CItem() {
    ClearFields();
    link_ = nullptr;
    spell_ = nullptr;
    SetMinusOne(condition_);
    SetMinusOne(conditionMax_);
    SetMinusOne(charge_);
    SetMinusOne(chargeMax_);
    SetMinusOne(chargeRate_);
    SetMinusOne(weight_);
    SetMinusOne(price_);
    SetMinusOne(value3C_);
    count_ = 1;
    DropSpell(spell_);
}

int CItem::Create() {
    count_ = 1;
    DropSpell(spell_);
    return 1;
}

void CItem::Destroy() {
    DropSpell(spell_);
    CRefCounted* link = Link(link_);
    link_ = nullptr;
    if (link != nullptr && --link->refs_ == 0) delete link;
}

CItem::~CItem() {
    CItem::Destroy();
    // The members' destructors: the spell released and the link's count dropped, without clearing.
    if (spell_ != nullptr) spell_->Release();
    CRefCounted* link = Link(link_);
    if (link != nullptr && --link->refs_ == 0) delete link;
}

CItemWeapon::CItemWeapon() {
    SetMinusOne(damage_);
    SetMinusOne(damageSpread_);
    SetMinusOne(reach_);
    actionValue_ = 0;
}

CItemArmor::CItemArmor() {
    std::memset(absorb_, 0, sizeof(absorb_));
    layerMask_[1] = 0;
    layerMask_[0] = 0;
}

CItemQuick::CItemQuick() = default;

CItemQuest::CItemQuest() = default;

CItemLoot::CItemLoot() = default;

CItemSpell::CItemSpell() = default;

const char* CItemWeapon::GetClassName() { return "CItemWeapon"; }
int CItemWeapon::GetClassId() { return 0x11; }
const char* CItemArmor::GetClassName() { return "CItemArmor"; }
int CItemArmor::GetClassId() { return 0x12; }
const char* CItemQuick::GetClassName() { return "CItemQuick"; }
int CItemQuick::GetClassId() { return 0x13; }
const char* CItemQuest::GetClassName() { return "CItemQuest"; }
int CItemQuest::GetClassId() { return 0x14; }
const char* CItemLoot::GetClassName() { return "CItemLoot"; }
int CItemLoot::GetClassId() { return 0x15; }
const char* CItemSpell::GetClassName() { return "CItemSpell"; }
int CItemSpell::GetClassId() { return 0x16; }

// --- The item manager's Create and factory -------------------------------------------------

int CItemManager::Create() { return CBaseManager::Create() != 0 ? 1 : 0; }


CEngineObject* CItemManager::NewObject(int kind) {
    CEngineObject* object;
    switch (kind) {
    case 0x3004: object = new CItemWeapon(); break;
    case 0x3005: object = new CItemArmor(); break;
    case 0x3006: object = new CItemQuick(); break;
    case 0x3007: object = new CItemLoot(); break;
    case 0x3008: object = new CItemSpell(); break;
    case 0x3009: object = new CItemQuest(); break;
    default: return nullptr;
    }
    if (object != nullptr) object->type_ = kind;
    return object;
}
