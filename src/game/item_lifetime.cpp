// The items' constructors and destructors, Create / Destroy, class names and ids, and the item manager's
// factory. See item.h and docs/notes/items_spells.md ("Item rules").
#include "item.h"
#include "spell.h"

CItem::CItem() {
    condition_ = -1.0f;
    conditionMax_ = -1.0f;
    charge_ = -1.0f;
    chargeMax_ = -1.0f;
    chargeRate_ = -1.0f;
    weight_ = -1.0f;
    price_ = -1.0f;
    unreadValue_ = -1.0f;
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
    damage_ = -1.0f;
    damageSpread_ = -1.0f;
    reach_ = -1.0f;
    actionValue_ = 0;
}

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

// --- The item manager's factory --------------------------------------------------------------------

// Default-initialised: the subclasses' own fields are set by InitFromRecord or Load.
CEngineObject* CItemManager::NewObject(int kind) {
    CEngineObject* object;
    switch (static_cast<ItemKind>(kind)) {
    case ItemKind::Weapon: object = new CItemWeapon; break;
    case ItemKind::Armor: object = new CItemArmor; break;
    case ItemKind::Quick: object = new CItemQuick; break;
    case ItemKind::Loot: object = new CItemLoot; break;
    case ItemKind::Spell: object = new CItemSpell; break;
    case ItemKind::Quest: object = new CItemQuest; break;
    default: return nullptr;
    }
    object->type_ = kind;
    return object;
}
