// The items' constructors and destructors, their Create / Destroy slots (2 / 3) and class names
// (11 / 12). See item.h and docs/notes/items_spells.md ("Item rules").
#include "item.h"

#include "core_refcount.h"
#include "spell.h"
#include "../lib/crt.h"

#include "ei/hook.h"

#include <cstring>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr std::uint32_t kMinusOne = 0xBF800000;   // -1.0f, stored as a dword

template <typename T>
void SetMinusOne(T& field) {
    static_assert(sizeof(T) == 4);
    std::memcpy(&field, &kMinusOne, 4);
}

// The item's link (+0x1C): a CRefCounted with a 16-bit count.
CRefCounted* Link(void* value) { return static_cast<CRefCounted*>(value); }

// The spell let go through its slot 4 (Release) after the pointer is cleared.
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
EI_REWRITTEN_SPECIAL(0x0069FCA0, CItem::CItem);

int CItem::Create() {
    count_ = 1;
    DropSpell(spell_);
    return 1;
}
EI_REWRITTEN(0x0069FE20, CItem::Create);

void CItem::Destroy() {
    DropSpell(spell_);
    CRefCounted* link = Link(link_);
    link_ = nullptr;
    if (link != nullptr && --link->refs_ == 0) delete link;
}
EI_REWRITTEN(0x0069FE50, CItem::Destroy);

CItem::~CItem() {
    CItem::Destroy();
    // The members' destructors: the spell released and the link's count dropped, without clearing.
    if (spell_ != nullptr) spell_->Release();
    CRefCounted* link = Link(link_);
    if (link != nullptr && --link->refs_ == 0) delete link;
}
// The destructor and its byte-identical copies per class (the weapon's, armour's, quick item's, quest
// item's, loot's and scroll's), then the scalar deleting destructors.
EI_REWRITTEN_SPECIAL(0x0069FD70, CItem::~CItem);
EI_REWRITTEN_SPECIAL(0x006A05B0, CItemWeapon::~CItemWeapon);
EI_REWRITTEN_SPECIAL(0x006A2FC0, CItemArmor::~CItemArmor);
EI_REWRITTEN_SPECIAL(0x006A3B30, CItemQuick::~CItemQuick);
EI_REWRITTEN_SPECIAL(0x006A4420, CItemQuest::~CItemQuest);
EI_REWRITTEN_SPECIAL(0x006A4A00, CItemLoot::~CItemLoot);
EI_REWRITTEN_SPECIAL(0x006A5740, CItemSpell::~CItemSpell);
EI_REWRITTEN_SPECIAL(0x0069FD30, CItem::~CItem);
EI_REWRITTEN_SPECIAL(0x006A0590, CItemWeapon::~CItemWeapon);
EI_REWRITTEN_SPECIAL(0x006A2FA0, CItemArmor::~CItemArmor);
EI_REWRITTEN_SPECIAL(0x006A3B10, CItemQuick::~CItemQuick);
EI_REWRITTEN_SPECIAL(0x006A4400, CItemQuest::~CItemQuest);
EI_REWRITTEN_SPECIAL(0x006A49E0, CItemLoot::~CItemLoot);
EI_REWRITTEN_SPECIAL(0x006A5720, CItemSpell::~CItemSpell);

CItemWeapon::CItemWeapon() {
    SetMinusOne(damage_);
    SetMinusOne(damageSpread_);
    SetMinusOne(reach_);
    actionValue_ = 0;
}
EI_REWRITTEN_SPECIAL(0x006A0540, CItemWeapon::CItemWeapon);

CItemArmor::CItemArmor() {
    std::memset(absorb_, 0, sizeof(absorb_));
    layerMask_[1] = 0;
    layerMask_[0] = 0;
}
EI_REWRITTEN_SPECIAL(0x006A2F20, CItemArmor::CItemArmor);

CItemQuick::CItemQuick() = default;
EI_REWRITTEN_SPECIAL(0x006A3AD0, CItemQuick::CItemQuick);

CItemQuest::CItemQuest() = default;
EI_REWRITTEN_SPECIAL(0x006A43A0, CItemQuest::CItemQuest);

CItemLoot::CItemLoot() = default;
EI_REWRITTEN_SPECIAL(0x006A49A0, CItemLoot::CItemLoot);

CItemSpell::CItemSpell() = default;
EI_REWRITTEN_SPECIAL(0x006A56E0, CItemSpell::CItemSpell);

// Slots 11 / 12: the exe's class names and ids.
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
EI_REWRITTEN(0x006A0570, CItemWeapon::GetClassName);
EI_REWRITTEN(0x006A0580, CItemWeapon::GetClassId);
EI_REWRITTEN(0x006A2F80, CItemArmor::GetClassName);
EI_REWRITTEN(0x006A2F90, CItemArmor::GetClassId);
EI_REWRITTEN(0x006A3AF0, CItemQuick::GetClassName);
EI_REWRITTEN(0x006A3B00, CItemQuick::GetClassId);
EI_REWRITTEN(0x006A43E0, CItemQuest::GetClassName);
EI_REWRITTEN(0x006A43F0, CItemQuest::GetClassId);
EI_REWRITTEN(0x006A49C0, CItemLoot::GetClassName);
EI_REWRITTEN(0x006A49D0, CItemLoot::GetClassId);
EI_REWRITTEN(0x006A5700, CItemSpell::GetClassName);
EI_REWRITTEN(0x006A5710, CItemSpell::GetClassId);

// --- The item manager's Create and factory (A-H25) -------------------------------------------------

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

EI_REWRITTEN(0x006AC780, CItemManager::Create);
EI_REWRITTEN(0x006AC810, CItemManager::NewObject);
