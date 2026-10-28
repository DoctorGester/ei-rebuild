// The items' constructors and destructors, their Create / Destroy slots (2 / 3) and class names
// (11 / 12). See item.h and docs/notes/items_spells.md ("Item rules").
#include "item.h"

#include "core_refcount.h"
#include "spell.h"

#include <bit>
#include <cstring>


namespace {

constexpr float kMinusOne = -1.0f;

template <typename T>
void SetMinusOne(T& field) {
    field = std::bit_cast<T>(kMinusOne);
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
    SetMinusOne(unreadValue_);
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

const char* CItemWeapon::ClassName() { return "CItemWeapon"; }
ClassId CItemWeapon::GetClassId() { return kClassItemWeapon; }
const char* CItemArmor::ClassName() { return "CItemArmor"; }
ClassId CItemArmor::GetClassId() { return kClassItemArmor; }
const char* CItemQuick::ClassName() { return "CItemQuick"; }
ClassId CItemQuick::GetClassId() { return kClassItemQuick; }
const char* CItemQuest::ClassName() { return "CItemQuest"; }
ClassId CItemQuest::GetClassId() { return kClassItemQuest; }
const char* CItemLoot::ClassName() { return "CItemLoot"; }
ClassId CItemLoot::GetClassId() { return kClassItemLoot; }
const char* CItemSpell::ClassName() { return "CItemSpell"; }
ClassId CItemSpell::GetClassId() { return kClassItemSpell; }

// --- The item manager's Create and factory -------------------------------------------------

int CItemManager::Create() { return CBaseManager::Create() != 0 ? 1 : 0; }


CEngineObject* CItemManager::NewObject(int kind) {
    CEngineObject* object;
    switch (kind) {
    case kItemWeapon: object = new CItemWeapon(); break;
    case kItemArmor: object = new CItemArmor(); break;
    case kItemQuick: object = new CItemQuick(); break;
    case kItemLoot: object = new CItemLoot(); break;
    case kItemSpell: object = new CItemSpell(); break;
    case kItemQuest: object = new CItemQuest(); break;
    default: return nullptr;
    }
    object->type_ = kind;
    return object;
}
