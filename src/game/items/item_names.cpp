// The names items build from their database records: model, figure and picture (inventory icon) names such as
// "sw_03.iron.1", "unhumasw_03.iron.1", "initwesw3weapon", "qitem0012.uq"; the weapon and armour model names (unit
// skins and models build the texture of a worn piece with them), the base figure and the figure parts a weapon is
// drawn with. See item_names.h and item.h.
#include "game/items/item_names.h"

#include "game/items/item.h"
#include "game/spells/spell.h"

#include "lib/str.h"

#include <cstdint>
#include <iterator>
#include <span>
#include <string>
#include <vector>

using namespace itemdb;

namespace {

// The prototype's number, -1 without a prototype.
int Number(const ItemRecordHead* record) { return record ? record->number : -1; }

// "unhuma" + the item's model name.
std::string HumanFigureName(Item* item) { return "unhuma" + item->ModelName(); }

// The weapon or armour picture name "initwe<code><number>weapon" / "initar<code><number>armor"; empty without
// the record.
std::string WeaponOrArmorPicture(bool weapon, const ItemRecordHead* record) {
    if (!record) return {};
    if (weapon) return str::Format("initwe%s%dweapon", TypeCode(kWeaponCodes, record->type), record->number);
    return str::Format("initar%s%darmor", TypeCode(kArmorCodes, record->type), record->number);
}

// One part name: prefix + the weapon's two-digit number + suffix, or a fixed name.
struct PartName {
    const char* prefix = nullptr;
    bool numbered = false;
    const char* suffix = nullptr;
};

constexpr PartName kSword[] = {{"rh3.sword", true, ""}};
constexpr PartName kAxe[] = {{"rh3.axe", true, ""}};
constexpr PartName kDagger[] = {{"rh3.dagger", true, ""}};
constexpr PartName kPike[] = {{"rh3.pike", true, ""}};
constexpr PartName kClub[] = {{"rh3.club", true, ""}};
constexpr PartName kBow[] = {
    {"lh3.bwpartb", true, ""}, {"bwparta", true, ""}, {"bwtetivaa", true, ""}, {"bwtetivab", true, ""},
    {"quiver", false, ""},     {"arrows", false, ""}, {"rh3.arrow00", false, ""},
};
constexpr PartName kCrossbow[] = {
    {"rh3.crbow", true, "main"},   {"crbow", true, "part01"}, {"crbow", true, "part02"},
    {"crbow", true, "tetiva01"},   {"crbow", true, "tetiva02"}, {"quiver", false, ""},
    {"arrows", false, ""},
};

// By WeaponType; other types have no parts.
const std::span<const PartName> kPartsByType[] = {kSword, kAxe, kDagger, kPike, kClub, kBow, kCrossbow};

} // namespace

// --- Item ---------------------------------------------------------------------------------------

constexpr const char* kScrollPicture = "initqi1item";

// A plain item has no names.
std::string Item::ModelName() { return {}; }
std::string Item::FigureName() { return {}; }
std::string Item::PictureName() { return {}; }

// --- Weapons and armors --------------------------------------------------------------------------

std::string ItemWeapon::ModelName() {
    return WeaponModelName(PrototypeIndex(prototypeId), MaterialIndex(prototypeId));
}

std::string ItemWeapon::FigureName() { return HumanFigureName(this); }

std::string ItemWeapon::PictureName() { return WeaponOrArmorPicture(true, WeaponAt(PrototypeIndex(prototypeId))); }

std::string ItemArmor::ModelName() {
    return ArmorModelName(PrototypeIndex(prototypeId), MaterialIndex(prototypeId));
}

std::string ItemArmor::FigureName() { return HumanFigureName(this); }

std::string ItemArmor::PictureName() { return WeaponOrArmorPicture(false, ArmorAt(PrototypeIndex(prototypeId))); }

// --- Quick items, quest items, scrolls -----------------------------------------------------------

std::string ItemQuick::FigureName() {
    const ItemRecordHead* prototype = GetPrototype();
    if (prototype && prototype->HasMaterial()) {
        const MaterialRecordHead* material = MaterialAt(MaterialIndex(prototypeId));
        return str::Format("qitem%04d.%s", prototype->number, material ? material->name.c_str() : "uq");
    }
    return str::Format("qitem%04d", Number(prototype));
}

std::string ItemQuick::PictureName() { return str::Format("initqi%ditem", Number(GetPrototype())); }

std::string ItemQuest::FigureName() { return str::Format("quitem%04d", Number(GetPrototype())); }

std::string ItemQuest::PictureName() { return str::Format("initqu%ditem", Number(GetPrototype())); }

std::string ItemScroll::FigureName() {
    return spell ? str::Format("spell%04d", spell->GetPrototype()->icon) : std::string();
}

std::string ItemScroll::PictureName() { return kScrollPicture; }

// --- Loot ----------------------------------------------------------------------------------------

std::string ItemLoot::FigureName() {
    if (kind == LootKind::Material) return str::Format("material%04d", index);
    if (kind != LootKind::Record) return str::Format("litem%04d", Number(GetPrototype()));
    switch (recordType) {
    case LootRecordType::Weapon:
    case LootRecordType::Armor: {
        bool weapon = recordType == LootRecordType::Weapon;
        const ItemRecordHead* record = WeaponOrArmorAt(weapon, index);
        if (!record) return {};
        return str::Format("%s_%.2d.%d", TypeCode(weapon ? kWeaponCodes : kArmorCodes, record->type), record->number,
                           record->variant);
    }
    case LootRecordType::Quick: {
        const QuickItemRecord* record = QuickItemAt(index);
        return record ? str::Format("qitem%04d", record->number) : std::string();
    }
    case LootRecordType::Spell: {
        const SpellRecord* record = SpellAt(index);
        return record ? str::Format("prototype%04d", record->icon) : std::string();
    }
    case LootRecordType::Modifier: return str::Format("modifier%04d", index);
    default: return {};
    }
}

std::string ItemLoot::PictureName() {
    if (kind == LootKind::Material) return str::Format("initlimt%ditem", index);
    if (kind != LootKind::Record) return str::Format("initlitr%ditem", Number(GetPrototype()));
    switch (recordType) {
    case LootRecordType::Weapon:
    case LootRecordType::Armor: {
        bool weapon = recordType == LootRecordType::Weapon;
        return WeaponOrArmorPicture(weapon, WeaponOrArmorAt(weapon, index));
    }
    case LootRecordType::Quick: {
        const QuickItemRecord* record = QuickItemAt(index);
        return record ? str::Format("initqi%ditem", record->number) : std::string();
    }
    case LootRecordType::Spell:
    case LootRecordType::Modifier: return "initqi1item";
    default: return {};
    }
}

// --- Model names, base figure and parts by record ------------------------------------------------

std::string RecordModelName(const ItemRecordHead* prototype, const char* const (&codes)[kItemTypeCodeCount],
                            const MaterialRecordHead* material) {
    if (!prototype || !material) return {};
    return str::Format("%s_%.2d.%s.%d", TypeCode(codes, prototype->type), prototype->number, material->name,
                       prototype->variant);
}

std::string ArmorModelName(std::uint32_t armorIndex, std::uint32_t materialIndex) {
    return RecordModelName(ArmorAt(armorIndex), kArmorCodes, MaterialAt(materialIndex));
}

std::string WeaponModelName(std::uint32_t weaponIndex, std::uint32_t materialIndex) {
    return RecordModelName(WeaponAt(weaponIndex), kWeaponCodes, MaterialAt(materialIndex));
}

std::string WeaponBaseFigure(std::uint32_t index) {
    const WeaponRecord* weapon = WeaponAt(index);
    if (!weapon) return {};
    // By WeaponType, Sword to Club.
    static const char* const kBases[] = {"basesword", "baseaxe", "basedagger", "basepike", "baseclub"};
    auto type = static_cast<WeaponType>(weapon->type);
    switch (type) {
    case WeaponType::Sword:
    case WeaponType::Axe:
    case WeaponType::Dagger:
    case WeaponType::Spear:
    case WeaponType::Club: return kBases[static_cast<int>(type)] + str::Format("%.2d", weapon->number);
    case WeaponType::Bow: return "basearrow00";
    case WeaponType::Crossbow: return "basearrow01";
    default: return "baser_hand3";
    }
}

std::vector<std::string> WeaponModelParts(std::uint32_t index) {
    std::vector<std::string> parts;
    const WeaponRecord* record = WeaponAt(index);
    if (!record) return parts;
    std::uint32_t type = static_cast<std::uint32_t>(record->type);
    if (type >= std::size(kPartsByType)) return parts;
    const std::string number = str::Format("%.2d", record->number);
    for (const PartName& part : kPartsByType[type]) {
        parts.push_back(part.numbered ? part.prefix + number + part.suffix : part.prefix);
    }
    return parts;
}
