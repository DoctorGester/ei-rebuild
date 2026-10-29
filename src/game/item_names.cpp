// Names the items build from their database records (virtuals of the item classes):
// model, figure and picture (inventory icon) names such as "sw_03.iron.1", "unhumasw_03.iron.1",
// "initwesw3weapon", "qitem0012.uq". Each returns a new string (by hidden pointer in the original).
#include "item.h"
#include "game_strings.h"
#include "spell.h"

#include "lib/str.h"
#include "lib/text_case.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

using namespace itemdb;

namespace {

// The code of `type`. It is copied into a 4-byte stack buffer that stays uninitialised for any other type (never
// the case in the databases); "" stands in for that.
const char* TypeCode(const char* const (&codes)[7], int type) {
    return type >= 0 && type < 7 ? codes[type] : "";
}

const char* const kModelFormat = "%s_%.2d.%s.%d";
const char* const kRecordModelFormat = "%s_%.2d.%d";
const char* const kWeaponPicture = "initwe%s%dweapon";
const char* const kArmorPicture = "initar%s%darmor";
const char* const kQuickFigure = "qitem%04d";
const char* const kQuickMaterialFigure = "qitem%04d.%s";
const char* const kQuickPicture = "initqi%ditem";
const char* const kSpellPicture = "initqi1item";
const char* const kQuestFigure = "quitem%04d";
const char* const kQuestPicture = "initqu%ditem";
const char* const kSpellFigure = "spell%04d";
const char* const kPrototypeFigure = "prototype%04d";
const char* const kModifierFigure = "modifier%04d";
const char* const kMaterialFigure = "material%04d";
const char* const kLootFigure = "litem%04d";
const char* const kMaterialPicture = "initlimt%ditem";
const char* const kLootPicture = "initlitr%ditem";
const char* const kHumanPrefix = "unhuma";
const char* const kNoMaterial = "none";
const char* const kUnknownMaterial = "uq";

// The records are read without a null check.

int Number(const ItemRecordHead* record) { return record ? record->number : -1; }

// "<code>_<number>.<material>.<variant>", or "" without a prototype or material.
template <typename Record>
std::string ModelNameOf(const NamedRecordTable<Record>& table, const char* const (&codes)[7], std::uint32_t id) {
    const MaterialRecordHead* material = MaterialAt(MaterialIndex(id));
    const Record* prototype = table.At(PrototypeIndex(id));
    std::string name;
    if (prototype && material)
        name = str::Format(kModelFormat, TypeCode(codes, prototype->type), prototype->number,
                    material->name, prototype->variant);
    return name;
}

// "unhuma" + the item's model name (a virtual call).
std::string HumanFigureName(CItem* item) { return kHumanPrefix + item->ModelName(); }

// The picture name "<format with code and number>" (no null check on the prototype).
template <typename Record>
std::string PictureNameOf(const NamedRecordTable<Record>& table, const char* const (&codes)[7], const char* format,
                           std::uint32_t id) {
    const Record* prototype = table.At(PrototypeIndex(id));
    const char* code = TypeCode(codes, prototype->type);
    std::string name;
    name = str::Format(format, code, prototype->number);
    return name;
}


} // namespace

// --- Weapons and armors ---------------------------------------------------------------------------

std::string CItemWeapon::ModelName() { return ModelNameOf(g_itemDatabase.weapons, kWeaponCodes, prototypeId_); }

std::string CItemWeapon::FigureName() { return HumanFigureName(this); }

std::string CItemWeapon::PictureName() {
    return PictureNameOf(g_itemDatabase.weapons, kWeaponCodes, kWeaponPicture, prototypeId_);
}

std::string CItemArmor::ModelName() { return ModelNameOf(g_itemDatabase.armors, kArmorCodes, prototypeId_); }

std::string CItemArmor::FigureName() { return HumanFigureName(this); }

std::string CItemArmor::PictureName() {
    return PictureNameOf(g_itemDatabase.armors, kArmorCodes, kArmorPicture, prototypeId_);
}

// The number is formatted into a 4-byte buffer just before the name buffer; a number above 999 runs into it
// (none in the database; the overlapping strcat that would follow is not reproduced).
bool WeaponBaseFigure(std::uint32_t index, std::string* out) {
    out->clear();
    if (index == kNoItemId) return false;
    const WeaponRecord* prototype = WeaponAt(index);
    if (!prototype) return false;
    char block[0x24];
    char* number = block;
    char* name = block + 4;
    std::snprintf(number, sizeof(block), "%.2d", prototype->number);
    static const char* const kBases[5] = {
        "basesword",
        "baseaxe",
        "basedagger",
        "basepike",
        "baseclub",
    };
    auto copy = [](char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); };
    std::uint32_t type = static_cast<std::uint32_t>(prototype->type);
    if (type < 5) {
        copy(name, kBases[type]);
        copy(name + std::strlen(name), number);
    } else if (type == 5) {
        copy(name, "basearrow00");
    } else if (type == 6) {
        copy(name, "basearrow01");
    } else {
        copy(name, "baser_hand3");
    }
    *out = name;
    return true;
}

// --- Quick items, quest items, scrolls -----------------------------------------------------------

std::string CItemQuick::FigureName() {
    std::string name;
    const ItemRecordHead* prototype = GetPrototype();
    if (prototype && text::CompareIgnoringCase(prototype->materialClass,
                                     kNoMaterial) != 0) {
        const MaterialRecordHead* material = MaterialAt(MaterialIndex(prototypeId_));
        const char* materialName = material ? material->name.c_str() : kUnknownMaterial;
        name = str::Format(kQuickMaterialFigure, Number(GetPrototype()), materialName);
        return name;
    }
    name = str::Format(kQuickFigure, Number(GetPrototype()));
    return name;
}

std::string CItemQuick::PictureName() {
    std::string name;
    name = str::Format(kQuickPicture, Number(GetPrototype()));
    return name;
}

std::string CItemQuest::FigureName() {
    std::string name;
    name = str::Format(kQuestFigure, Number(GetPrototype()));
    return name;
}

std::string CItemQuest::PictureName() {
    std::string name;
    name = str::Format(kQuestPicture, Number(GetPrototype()));
    return name;
}

std::string CItemSpell::FigureName() {
    const SpellRecord* prototype = SpellAt(PrototypeIndex(spell_->prototypeId_));
    std::string name;
    name = str::Format(kSpellFigure, prototype->icon);
    return name;
}

std::string CItemSpell::PictureName() { return std::string(kSpellPicture); }

// --- Loot ----------------------------------------------------------------------------------------

std::string CItemLoot::FigureName() {
    std::string name;
    if (kind_ == LootKind::Record) {
        switch (recordType_) {
        case LootRecordType::Weapon:
        case LootRecordType::Armor: {
            bool weapon = recordType_ == LootRecordType::Weapon;
            const ItemRecordHead* record = WeaponOrArmorAt(weapon, index_);
            std::string code;
            int type = record->type;
            if (type >= 0 && type < 7) code = (weapon ? kWeaponCodes : kArmorCodes)[type];
            name = str::Format(kRecordModelFormat, code, record->number, record->variant);
            break;
        }
        case LootRecordType::Quick:
            name = str::Format(kQuickFigure, QuickItemAt(index_)->number);
            break;
        case LootRecordType::Spell:
            name = str::Format(kPrototypeFigure, SpellAt(index_)->icon);
            break;
        case LootRecordType::Modifier:
            name = str::Format(kModifierFigure, index_);
            break;
        default:
            break;
        }
        return name;
    }
    if (kind_ == LootKind::Material)
        name = str::Format(kMaterialFigure, index_);
    else
        name = str::Format(kLootFigure, Number(GetPrototype()));
    return name;
}

std::string CItemLoot::PictureName() {
    std::string name;
    if (kind_ == LootKind::Record) {
        switch (recordType_) {
        case LootRecordType::Weapon:
        case LootRecordType::Armor: {
            bool weapon = recordType_ == LootRecordType::Weapon;
            const ItemRecordHead* record = WeaponOrArmorAt(weapon, index_);
            std::string code;
            int type = record->type;
            if (type >= 0 && type < 7) code = (weapon ? kWeaponCodes : kArmorCodes)[type];
            name = str::Format(weapon ? kWeaponPicture : kArmorPicture, code, record->number);
            break;
        }
        case LootRecordType::Quick:
            name = str::Format(kQuickPicture, QuickItemAt(index_)->number);
            break;
        case LootRecordType::Spell:
        case LootRecordType::Modifier:
            name = kSpellPicture;
            break;
        default:
            break;
        }
        return name;
    }
    if (kind_ == LootKind::Material)
        name = str::Format(kMaterialPicture, index_);
    else
        name = str::Format(kLootPicture, Number(GetPrototype()));
    return name;
}
