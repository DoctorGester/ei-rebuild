// The names items build from their database records: model, figure and picture (inventory icon) names such as
// "sw_03.iron.1", "unhumasw_03.iron.1", "initwesw3weapon", "qitem0012.uq"; the weapon and armour model names (unit
// skins and models build the texture of a worn piece with them), the base figure and the figure parts a weapon is
// drawn with. See item_names.h and item.h.
#include "item_names.h"

#include "item.h"
#include "spell.h"

#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

// One part name: prefix + the weapon's two-digit number + suffix, or a fixed name.
struct PartName {
    const char* prefix;
    bool numbered;
    const char* suffix;
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

struct PartSpan {
    const PartName* parts;
    int count;
};

template <int N> constexpr PartSpan List(const PartName (&parts)[N]) { return {parts, N}; }

// By weapon type; other types have no parts.
constexpr PartSpan kPartsByType[] = {List(kSword), List(kAxe),  List(kDagger),  List(kPike),
                                     List(kClub),  List(kBow),  List(kCrossbow)};

// Calls add(name) for each part of the weapon prototype `record` (not null), in the game's order.
template <typename Add> void ForEachPart(const WeaponRecord* head, Add add) {
    char number[8];
    std::snprintf(number, sizeof(number), "%.2d", head->number);
    std::uint32_t type = static_cast<std::uint32_t>(head->type);
    if (type >= sizeof(kPartsByType) / sizeof(kPartsByType[0])) return;
    const PartSpan& list = kPartsByType[type];
    for (int i = 0; i < list.count; ++i) {
        const PartName& part = list.parts[i];
        if (!part.numbered) {
            add(part.prefix);
            continue;
        }
        char name[64];
        char* end = name;
        const char* pieces[] = {part.prefix, number, part.suffix};
        for (const char* piece : pieces) {
            std::size_t n = std::strlen(piece);
            std::memcpy(end, piece, n);
            end += n;
        }
        *end = '\0';
        add(static_cast<const char*>(name));
    }
}

} // namespace

// --- CItem ---------------------------------------------------------------------------------------

// A plain item has no names.
std::string CItem::ModelName() { return {}; }
std::string CItem::FigureName() { return {}; }
std::string CItem::PictureName() { return {}; }

// --- Weapons and armors --------------------------------------------------------------------------

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

// --- Model names, base figure and parts by record ------------------------------------------------

std::string RecordModelName(const ItemRecordHead* prototype, const char* const (&codes)[7],
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
    static const char* const kBases[5] = {"basesword", "baseaxe", "basedagger", "basepike", "baseclub"};
    std::uint32_t type = static_cast<std::uint32_t>(weapon->type);
    if (type < 5) return kBases[type] + str::Format("%.2d", weapon->number);
    if (type == 5) return "basearrow00";
    if (type == 6) return "basearrow01";
    return "baser_hand3";
}

std::vector<std::string> WeaponModelParts(std::uint32_t index) {
    std::vector<std::string> parts;
    const WeaponRecord* record = WeaponAt(index);
    if (!record) return parts;
    ForEachPart(record, [&parts](const char* name) { parts.push_back(name); });
    return parts;
}

void AppendWeaponModelParts(std::uint32_t packedId, std::vector<std::string>* out) {
    const WeaponRecord* record = WeaponAt(PrototypeIndex(packedId));
    if (!record) return;
    ForEachPart(record, [out](const char* name) {
        std::string s(name);
        out->push_back(s);
    });
}
