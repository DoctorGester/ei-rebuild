// Names the items build from their database records (virtuals of the item classes): model, figure and picture
// (inventory icon) names such as "sw_03.iron.1", "unhumasw_03.iron.1", "initwesw3weapon", "qitem0012.uq".
#include "item.h"
#include "spell.h"

#include "lib/str.h"
#include "lib/text_case.h"

#include <string>

using namespace itemdb;

namespace {

// The prototype's number, -1 without a prototype.
int Number(const ItemRecordHead* record) { return record ? record->number : -1; }

// "unhuma" + the item's model name.
std::string HumanFigureName(CItem* item) { return "unhuma" + item->ModelName(); }

// The weapon or armour picture name "initwe<code><number>weapon" / "initar<code><number>armor" (no null check on
// the record).
std::string WeaponOrArmorPicture(bool weapon, const ItemRecordHead* record) {
    if (weapon) return str::Format("initwe%s%dweapon", TypeCode(kWeaponCodes, record->type), record->number);
    return str::Format("initar%s%darmor", TypeCode(kArmorCodes, record->type), record->number);
}

} // namespace

// --- Weapons and armors ---------------------------------------------------------------------------

std::string CItemWeapon::ModelName() {
    return WeaponModelName(PrototypeIndex(prototypeId_), MaterialIndex(prototypeId_));
}

std::string CItemWeapon::FigureName() { return HumanFigureName(this); }

std::string CItemWeapon::PictureName() { return WeaponOrArmorPicture(true, WeaponAt(PrototypeIndex(prototypeId_))); }

std::string CItemArmor::ModelName() {
    return ArmorModelName(PrototypeIndex(prototypeId_), MaterialIndex(prototypeId_));
}

std::string CItemArmor::FigureName() { return HumanFigureName(this); }

std::string CItemArmor::PictureName() { return WeaponOrArmorPicture(false, ArmorAt(PrototypeIndex(prototypeId_))); }

std::string WeaponBaseFigure(std::uint32_t index) {
    const WeaponRecord* weapon = WeaponAt(index);
    if (!weapon) return {};
    static const char* const kBases[5] = {"basesword", "baseaxe", "basedagger", "basepike", "baseclub"};
    std::uint32_t type = static_cast<std::uint32_t>(weapon->type);
    if (type < 5) return kBases[type] + str::Format("%.2d", weapon->number);
    if (type == 5) return "basearrow00";
    if (type == 6) return "basearrow01";
    return "baser_hand3";
}

// --- Quick items, quest items, scrolls -----------------------------------------------------------

std::string CItemQuick::FigureName() {
    const ItemRecordHead* prototype = GetPrototype();
    if (prototype && text::CompareIgnoringCase(prototype->materialClass, "none") != 0) {
        const MaterialRecordHead* material = MaterialAt(MaterialIndex(prototypeId_));
        return str::Format("qitem%04d.%s", prototype->number, material ? material->name.c_str() : "uq");
    }
    return str::Format("qitem%04d", Number(prototype));
}

std::string CItemQuick::PictureName() { return str::Format("initqi%ditem", Number(GetPrototype())); }

std::string CItemQuest::FigureName() { return str::Format("quitem%04d", Number(GetPrototype())); }

std::string CItemQuest::PictureName() { return str::Format("initqu%ditem", Number(GetPrototype())); }

std::string CItemSpell::FigureName() {
    return str::Format("spell%04d", SpellAt(PrototypeIndex(spell_->prototypeId_))->icon);
}

std::string CItemSpell::PictureName() { return "initqi1item"; }

// --- Loot ----------------------------------------------------------------------------------------

std::string CItemLoot::FigureName() {
    if (kind_ == LootKind::Material) return str::Format("material%04d", index_);
    if (kind_ != LootKind::Record) return str::Format("litem%04d", Number(GetPrototype()));
    switch (recordType_) {
    case LootRecordType::Weapon:
    case LootRecordType::Armor: {
        bool weapon = recordType_ == LootRecordType::Weapon;
        const ItemRecordHead* record = WeaponOrArmorAt(weapon, index_);
        return str::Format("%s_%.2d.%d", TypeCode(weapon ? kWeaponCodes : kArmorCodes, record->type), record->number,
                           record->variant);
    }
    case LootRecordType::Quick: return str::Format("qitem%04d", QuickItemAt(index_)->number);
    case LootRecordType::Spell: return str::Format("prototype%04d", SpellAt(index_)->icon);
    case LootRecordType::Modifier: return str::Format("modifier%04d", index_);
    default: return {};
    }
}

std::string CItemLoot::PictureName() {
    if (kind_ == LootKind::Material) return str::Format("initlimt%ditem", index_);
    if (kind_ != LootKind::Record) return str::Format("initlitr%ditem", Number(GetPrototype()));
    switch (recordType_) {
    case LootRecordType::Weapon:
    case LootRecordType::Armor: {
        bool weapon = recordType_ == LootRecordType::Weapon;
        return WeaponOrArmorPicture(weapon, WeaponOrArmorAt(weapon, index_));
    }
    case LootRecordType::Quick: return str::Format("initqi%ditem", QuickItemAt(index_)->number);
    case LootRecordType::Spell:
    case LootRecordType::Modifier: return "initqi1item";
    default: return {};
    }
}
