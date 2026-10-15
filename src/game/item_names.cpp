// Names the items build from their database records (virtual slots 17-20 of the item classes):
// model, figure and picture (inventory icon) names such as "sw_03.iron.1", "unhumasw_03.iron.1",
// "initwesw3weapon", "qitem0012.uq". Each returns a new CString (by hidden pointer, as MFC does).
#include "item.h"
#include "game_strings.h"
#include "spell.h"

#include "console.h"
#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>
#include <new>

using namespace itemdb;

namespace {

// The code of `type`. The original copies it into a 4-byte stack buffer and leaves the buffer
// uninitialised for any other type (never the case in the databases); "" stands in for that.
const char* TypeCode(const char* const (&codes)[7], int type) {
    return type >= 0 && type < 7 ? codes[type] : "";
}

const char* const kModelFormat = game::Text(0x007963D0);        // "%s_%.2d.%s.%d"
const char* const kRecordModelFormat = game::Text(0x007965EC);  // "%s_%.2d.%d"
const char* const kWeaponPicture = game::Text(0x00796514);      // "initwe%s%dweapon"
const char* const kArmorPicture = game::Text(0x00796550);       // "initar%s%darmor"
const char* const kQuickFigure = game::Text(0x0079656C);        // "qitem%04d"
const char* const kQuickMaterialFigure = game::Text(0x00796578);// "qitem%04d.%s"
const char* const kQuickPicture = game::Text(0x00796588);       // "initqi%ditem"
const char* const kSpellPicture = game::Text(0x0079457C);       // "initqi1item"
const char* const kQuestFigure = game::Text(0x007934D0);        // "quitem%04d"
const char* const kQuestPicture = game::Text(0x007934DC);       // "initqu%ditem"
const char* const kSpellFigure = game::Text(0x007945D8);        // "spell%04d"
const char* const kPrototypeFigure = game::Text(0x007965DC);    // "prototype%04d"
const char* const kModifierFigure = game::Text(0x007965CC);     // "modifier%04d"
const char* const kMaterialFigure = game::Text(0x007965BC);     // "material%04d"
const char* const kLootFigure = game::Text(0x007965B0);         // "litem%04d"
const char* const kMaterialPicture = game::Text(0x00796608);    // "initlimt%ditem"
const char* const kLootPicture = game::Text(0x007965F8);        // "initlitr%ditem"
const char* const kHumanPrefix = game::Text(0x007908C8);        // "unhuma"
const char* const kNoMaterial = game::Text(0x007846F0);         // "none"
const char* const kUnknownMaterial = game::Text(0x007963C0);    // "uq"

// The records are read without a null check where the original reads them without one.
const ItemRecordHead* Head(const char* record) { return reinterpret_cast<const ItemRecordHead*>(record); }
const MaterialRecordHead* Material(const char* record) { return reinterpret_cast<const MaterialRecordHead*>(record); }

int Number(const char* record) { return record ? Head(record)->number : -1; }

// "<code>_<number>.<material>.<variant>", or "" without a prototype or material (slot 17).
mfc::CString ModelNameOf(const RecordTable& table, int stride, const char* const (&codes)[7], std::uint32_t id) {
    const char* material = Materials().At(MaterialIndex(id), kMaterialSize);
    const char* prototype = table.At(PrototypeIndex(id), stride);
    mfc::CString name;
    if (prototype && material)
        name.Format(kModelFormat, TypeCode(codes, Head(prototype)->type), Head(prototype)->number,
                    Material(material)->name, Head(prototype)->variant);
    return name;
}

// The first string of the prototype's list, or "" (slot 18).
mfc::CString FirstListNameOf(const RecordTable& table, int stride, std::uint32_t id) {
    const char* prototype = table.At(PrototypeIndex(id), stride);
    const console::CStringArray* list = &Head(prototype)->list20;
    if (prototype && list->GetSize() != 0) return mfc::CString((*list)[0]);
    return mfc::CString(game::EmptyText());   // ""
}

// "unhuma" + the item's slot 17 (a virtual call). The model name is destroyed before the prefix,
// as in the original.
mfc::CString HumanFigureName(CItem* item) {
    alignas(mfc::CString) char model[sizeof(mfc::CString)];
    mfc::CString& modelName = *new (model) mfc::CString(item->ModelName());
    mfc::CString prefix(kHumanPrefix);
    mfc::CString name = prefix + modelName;
    modelName.~CString();
    return name;
}

// The picture name "<format with code and number>" (slot 20; no null check on the prototype).
mfc::CString PictureNameOf(const RecordTable& table, int stride, const char* const (&codes)[7], const char* format,
                           std::uint32_t id) {
    const char* prototype = table.At(PrototypeIndex(id), stride);
    const char* code = TypeCode(codes, Head(prototype)->type);
    mfc::CString name;
    name.Format(format, code, Head(prototype)->number);
    return name;
}

} // namespace

// --- Weapons and armors ---------------------------------------------------------------------------

mfc::CString CItemWeapon::ModelNameImpl() { return ModelNameOf(Weapons(), kWeaponSize, kWeaponCodes, prototypeId_); }

mfc::CString CItemWeapon::FirstListNameImpl() { return FirstListNameOf(Weapons(), kWeaponSize, prototypeId_); }

mfc::CString CItemWeapon::FigureNameImpl() { return HumanFigureName(this); }

mfc::CString CItemWeapon::PictureNameImpl() {
    return PictureNameOf(Weapons(), kWeaponSize, kWeaponCodes, kWeaponPicture, prototypeId_);
}

mfc::CString CItemArmor::ModelNameImpl() { return ModelNameOf(Armors(), kArmorSize, kArmorCodes, prototypeId_); }

mfc::CString CItemArmor::FirstListNameImpl() { return FirstListNameOf(Armors(), kArmorSize, prototypeId_); }

mfc::CString CItemArmor::FigureNameImpl() { return HumanFigureName(this); }

mfc::CString CItemArmor::PictureNameImpl() {
    return PictureNameOf(Armors(), kArmorSize, kArmorCodes, kArmorPicture, prototypeId_);
}

// 006A0CC0. The original formats the number into a 4-byte buffer just before the name buffer; a
// number above 999 would overrun it (none in the database; the overlapping strcat that would
// follow is not reproduced).
bool __fastcall WeaponBaseFigure(std::uint32_t index, mfc::CString* out) {
    out->Empty();
    if (index == 0xFFFFFFFFu) return false;
    const char* prototype = Weapons().At(index, kWeaponSize);
    if (!prototype) return false;
    char block[0x24];
    char* number = block;
    char* name = block + 4;
    gamecrt::sprintf(number, game::Text(0x00796454), Head(prototype)->number);   // "%.2d"
    static const std::uintptr_t kBases[5] = {
        0x00796448,   // "basesword"
        0x00796440,   // "baseaxe"
        0x00796434,   // "basedagger"
        0x00796428,   // "basepike"
        0x0079641C,   // "baseclub"
    };
    auto copy = [](char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); };
    std::uint32_t type = static_cast<std::uint32_t>(Head(prototype)->type);
    if (type < 5) {
        copy(name, game::Text(kBases[type]));
        copy(name + std::strlen(name), number);
    } else if (type == 5) {
        copy(name, game::Text(0x00796410));   // "basearrow00"
    } else if (type == 6) {
        copy(name, game::Text(0x00796404));   // "basearrow01"
    } else {
        copy(name, game::Text(0x007963F8));   // "baser_hand3"
    }
    *out = name;
    return true;
}
EI_REPLACE(0x006A0CC0, WeaponBaseFigure);

// --- Quick items, quest items, scrolls -----------------------------------------------------------

mfc::CString CItemQuick::FigureNameImpl() {
    mfc::CString name;
    const char* prototype = GetPrototype();
    if (prototype && gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(Head(prototype)->materialClass)),
                                      reinterpret_cast<const unsigned char*>(kNoMaterial)) != 0) {
        const char* material = Materials().At(MaterialIndex(prototypeId_), kMaterialSize);
        const char* materialName = material ? Material(material)->name : kUnknownMaterial;
        name.Format(kQuickMaterialFigure, Number(GetPrototype()), materialName);
        return name;
    }
    name.Format(kQuickFigure, Number(GetPrototype()));
    return name;
}

mfc::CString CItemQuick::PictureNameImpl() {
    mfc::CString name;
    name.Format(kQuickPicture, Number(GetPrototype()));
    return name;
}

mfc::CString CItemQuest::FigureNameImpl() {
    mfc::CString name;
    name.Format(kQuestFigure, Number(GetPrototype()));
    return name;
}

mfc::CString CItemQuest::PictureNameImpl() {
    mfc::CString name;
    name.Format(kQuestPicture, Number(GetPrototype()));
    return name;
}

mfc::CString CItemSpell::FigureNameImpl() {
    const SpellRecord* prototype = SpellAt(PrototypeIndex(spell_->prototypeId_));
    mfc::CString name;
    name.Format(kSpellFigure, prototype->icon);
    return name;
}

mfc::CString CItemSpell::PictureNameImpl() { return mfc::CString(kSpellPicture); }

// --- Loot ----------------------------------------------------------------------------------------

namespace {
enum LootKind : std::uint32_t { kLootSelf = 0, kLootMaterial = 1, kLootRecord = 2 };
enum LootRecord : std::uint32_t { kRecordWeapon, kRecordArmor, kRecordQuick, kRecordSpell, kRecordModifier };
} // namespace

mfc::CString CItemLoot::FigureNameImpl() {
    mfc::CString name;
    if (kind_ == kLootRecord) {
        switch (recordType_) {
        case kRecordWeapon:
        case kRecordArmor: {
            bool weapon = recordType_ == kRecordWeapon;
            const char* record = weapon ? Weapons().At(index_, kWeaponSize) : Armors().At(index_, kArmorSize);
            mfc::CString code;
            int type = Head(record)->type;
            if (type >= 0 && type < 7) code = (weapon ? kWeaponCodes : kArmorCodes)[type];
            name.Format(kRecordModelFormat, static_cast<const char*>(code), Head(record)->number,
                        Head(record)->variant);
            break;
        }
        case kRecordQuick:
            name.Format(kQuickFigure, Head(QuickItems().At(index_, kQuickItemSize))->number);
            break;
        case kRecordSpell:
            name.Format(kPrototypeFigure, SpellAt(index_)->icon);
            break;
        case kRecordModifier:
            name.Format(kModifierFigure, index_);
            break;
        default:
            break;
        }
        return name;
    }
    if (kind_ == kLootMaterial)
        name.Format(kMaterialFigure, index_);
    else
        name.Format(kLootFigure, Number(GetPrototype()));
    return name;
}

mfc::CString CItemLoot::PictureNameImpl() {
    mfc::CString name;
    if (kind_ == kLootRecord) {
        switch (recordType_) {
        case kRecordWeapon:
        case kRecordArmor: {
            bool weapon = recordType_ == kRecordWeapon;
            const char* record = weapon ? Weapons().At(index_, kWeaponSize) : Armors().At(index_, kArmorSize);
            mfc::CString code;
            int type = Head(record)->type;
            if (type >= 0 && type < 7) code = (weapon ? kWeaponCodes : kArmorCodes)[type];
            name.Format(weapon ? kWeaponPicture : kArmorPicture, static_cast<const char*>(code),
                        Head(record)->number);
            break;
        }
        case kRecordQuick:
            name.Format(kQuickPicture, Head(QuickItems().At(index_, kQuickItemSize))->number);
            break;
        case kRecordSpell:
        case kRecordModifier:
            name = kSpellPicture;
            break;
        default:
            break;
        }
        return name;
    }
    if (kind_ == kLootMaterial)
        name.Format(kMaterialPicture, index_);
    else
        name.Format(kLootPicture, Number(GetPrototype()));
    return name;
}

// --- The virtual slots' entry points (rule 4): the name built in the caller's result -------------

EI_REPLACE_METHOD(0x006A0840, CItemWeapon::ModelNameImpl);
EI_REPLACE_METHOD(0x006A0F70, CItemWeapon::FirstListNameImpl);
EI_REPLACE_METHOD(0x006A2750, CItemWeapon::FigureNameImpl);
EI_REPLACE_METHOD(0x006A27E0, CItemWeapon::PictureNameImpl);
EI_REPLACE_METHOD(0x006A3240, CItemArmor::ModelNameImpl);
EI_REPLACE_METHOD(0x006A36A0, CItemArmor::FirstListNameImpl);
EI_REPLACE_METHOD(0x006A3720, CItemArmor::FigureNameImpl);
EI_REPLACE_METHOD(0x006A37B0, CItemArmor::PictureNameImpl);
EI_REPLACE_METHOD(0x006A3CD0, CItemQuick::FigureNameImpl);
EI_REPLACE_METHOD(0x006A4040, CItemQuick::PictureNameImpl);
EI_REPLACE_METHOD(0x006A45C0, CItemQuest::FigureNameImpl);
EI_REPLACE_METHOD(0x006A47B0, CItemQuest::PictureNameImpl);
EI_REPLACE_METHOD(0x006A5820, CItemSpell::FigureNameImpl);
EI_REPLACE_METHOD(0x006A58E0, CItemSpell::PictureNameImpl);
EI_REPLACE_METHOD(0x006A4B90, CItemLoot::FigureNameImpl);
EI_REPLACE_METHOD(0x006A5030, CItemLoot::PictureNameImpl);
