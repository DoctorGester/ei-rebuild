// The items' constructors and destructors, their Create / Destroy slots (2 / 3) and class names
// (11 / 12). See item.h and docs/notes/items_spells.md ("Item rules").
#include "core_refcount.h"
#include "item.h"
#include "spell.h"

#include <bit>
#include <cstring>

namespace {

constexpr float kMinusOne = -1.0f;

template <typename T>
void SetMinusOne(T& field) {
    field = std::bit_cast<T>(kMinusOne);
}

} // namespace

CItem::CItem() {
    SetMinusOne(condition_);
    SetMinusOne(conditionMax_);
    SetMinusOne(charge_);
    SetMinusOne(chargeMax_);
    SetMinusOne(chargeRate_);
    SetMinusOne(weight_);
    SetMinusOne(price_);
    SetMinusOne(unreadValue_);
    count_ = 1;
}

void CItem::Create() {
    count_ = 1;
    spell_.Reset();
}

void CItem::Destroy() {
    spell_.Reset();
    link_.Reset();
}

CItem::~CItem() {
    CItem::Destroy();
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

const char* CItemWeapon::ClassName() { return "CItemWeapon"; }
ClassId CItemWeapon::GetClassId() { return ClassId::ItemWeapon; }
const char* CItemArmor::ClassName() { return "CItemArmor"; }
ClassId CItemArmor::GetClassId() { return ClassId::ItemArmor; }
const char* CItemQuick::ClassName() { return "CItemQuick"; }
ClassId CItemQuick::GetClassId() { return ClassId::ItemQuick; }
const char* CItemQuest::ClassName() { return "CItemQuest"; }
ClassId CItemQuest::GetClassId() { return ClassId::ItemQuest; }
const char* CItemLoot::ClassName() { return "CItemLoot"; }
ClassId CItemLoot::GetClassId() { return ClassId::ItemLoot; }
const char* CItemSpell::ClassName() { return "CItemSpell"; }
ClassId CItemSpell::GetClassId() { return ClassId::ItemSpell; }

// --- The item manager's Create and factory -------------------------------------------------



CEngineObject* CItemManager::NewObject(int kind) {
    CEngineObject* object;
    switch (static_cast<ItemKind>(kind)) {
    case ItemKind::Weapon: object = new CItemWeapon(); break;
    case ItemKind::Armor: object = new CItemArmor(); break;
    case ItemKind::Quick: object = new CItemQuick(); break;
    case ItemKind::Loot: object = new CItemLoot(); break;
    case ItemKind::Spell: object = new CItemSpell(); break;
    case ItemKind::Quest: object = new CItemQuest(); break;
    default: return nullptr;
    }
    object->type_ = kind;
    return object;
}
