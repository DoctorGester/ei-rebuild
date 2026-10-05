// Names the items build from their database records (virtual slots 17-20 of the item classes):
// model, figure and picture (inventory icon) names such as "sw_03.iron.1", "unhumasw_03.iron.1",
// "initwesw3weapon", "qitem0012.uq". Each returns a new CString (by hidden pointer, as MFC does).
#include "item.h"
#include "game_strings.h"

#include "console.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

using namespace itemdb;

namespace {

constexpr int kSlotModelName = 17;   // +0x44

// The two-letter type codes of weapons (007963E0..007963F4, "hm" at 00784524) and armors
// (00796534..0079654C), by type.
constexpr const char* kWeaponCodes[7] = {"sw", "ax", "dg", "sp", "hm", "bw", "cb"};
constexpr const char* kArmorCodes[7] = {"hl", "pl", "lg", "sh", "pt", "bt", "gl"};

// The code of `type`. The original copies it into a 4-byte stack buffer and leaves the buffer
// uninitialised for any other type (never the case in the databases); "" stands in for that.
const char* TypeCode(const char* const (&codes)[7], int type) {
    return type >= 0 && type < 7 ? codes[type] : "";
}

// The game's format strings and names (by address, as the original passes them).
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
const char* const kModelFormat = Text(0x007963D0);        // "%s_%.2d.%s.%d"
const char* const kRecordModelFormat = Text(0x007965EC);  // "%s_%.2d.%d"
const char* const kWeaponPicture = Text(0x00796514);      // "initwe%s%dweapon"
const char* const kArmorPicture = Text(0x00796550);       // "initar%s%darmor"
const char* const kQuickFigure = Text(0x0079656C);        // "qitem%04d"
const char* const kQuickMaterialFigure = Text(0x00796578);// "qitem%04d.%s"
const char* const kQuickPicture = Text(0x00796588);       // "initqi%ditem"
const char* const kSpellPicture = Text(0x0079457C);       // "initqi1item"
const char* const kQuestFigure = Text(0x007934D0);        // "quitem%04d"
const char* const kQuestPicture = Text(0x007934DC);       // "initqu%ditem"
const char* const kSpellFigure = Text(0x007945D8);        // "spell%04d"
const char* const kPrototypeFigure = Text(0x007965DC);    // "prototype%04d"
const char* const kModifierFigure = Text(0x007965CC);     // "modifier%04d"
const char* const kMaterialFigure = Text(0x007965BC);     // "material%04d"
const char* const kLootFigure = Text(0x007965B0);         // "litem%04d"
const char* const kMaterialPicture = Text(0x00796608);    // "initlimt%ditem"
const char* const kLootPicture = Text(0x007965F8);        // "initlitr%ditem"
const char* const kHumanPrefix = Text(0x007908C8);        // "unhuma"
const char* const kNoMaterial = Text(0x007846F0);         // "none"
const char* const kUnknownMaterial = Text(0x007963C0);    // "uq"

// Record fields used here.
constexpr int kRecordNumber = 0x18;     // the number in the names
constexpr int kRecordVariant = 0x1C;    // the model variant
constexpr int kRecordNameList = 0x20;   // CStringArray
constexpr int kMaterialName = 0x08;     // char* (CString)
constexpr int kQuickMaterial = 0x10;    // char* (CString): "none" for items without a material
constexpr int kSpellFigureNumber = 0x60;

int Number(const char* record) { return record ? Field<int>(record, kRecordNumber) : -1; }

// "<code>_<number>.<material>.<variant>", or "" without a prototype or material (slot 17).
mfc::CString ModelNameOf(const RecordTable& table, int stride, const char* const (&codes)[7], std::uint32_t id) {
    const char* material = Materials().At(MaterialIndex(id), kMaterialSize);
    const char* prototype = table.At(PrototypeIndex(id), stride);
    mfc::CString name;
    if (prototype && material)
        name.Format(kModelFormat, TypeCode(codes, Field<int>(prototype, field::kItemType)),
                    Field<int>(prototype, kRecordNumber), Field<const char*>(material, kMaterialName),
                    Field<int>(prototype, kRecordVariant));
    return name;
}

// The first string of the prototype's list, or "" (slot 18).
mfc::CString FirstListNameOf(const RecordTable& table, int stride, std::uint32_t id) {
    const char* prototype = table.At(PrototypeIndex(id), stride);
    auto* list = reinterpret_cast<const console::CStringArray*>(prototype + kRecordNameList);
    if (prototype && list->GetSize() != 0) return mfc::CString((*list)[0]);
    return mfc::CString(game::EmptyText());   // ""
}

// "unhuma" + the item's slot 17 (called through its vtable).
mfc::CString HumanFigureName(CItem* item) {
    char model[sizeof(mfc::CString)];   // constructed by the callee
    ei::VCall<void*>(item, kSlotModelName, static_cast<void*>(model));
    mfc::CString& modelName = *reinterpret_cast<mfc::CString*>(model);
    mfc::CString prefix(kHumanPrefix);
    mfc::CString name = prefix + modelName;
    modelName.~CString();
    return name;
}

// The picture name "<format with code and number>" (slot 20; no null check on the prototype).
mfc::CString PictureNameOf(const RecordTable& table, int stride, const char* const (&codes)[7], const char* format,
                           std::uint32_t id) {
    const char* prototype = table.At(PrototypeIndex(id), stride);
    const char* code = TypeCode(codes, Field<int>(prototype, field::kItemType));
    mfc::CString name;
    name.Format(format, code, Field<int>(prototype, kRecordNumber));
    return name;
}

} // namespace

// --- Weapons and armors ---------------------------------------------------------------------------

mfc::CString CItemWeapon::ModelName() { return ModelNameOf(Weapons(), kWeaponSize, kWeaponCodes, unknown_18); }
EI_REPLACE_METHOD(0x006A0840, CItemWeapon::ModelName);

mfc::CString CItemWeapon::FirstListName() { return FirstListNameOf(Weapons(), kWeaponSize, unknown_18); }
EI_REPLACE_METHOD(0x006A0F70, CItemWeapon::FirstListName);

mfc::CString CItemWeapon::FigureName() { return HumanFigureName(this); }
EI_REPLACE_METHOD(0x006A2750, CItemWeapon::FigureName);

mfc::CString CItemWeapon::PictureName() {
    return PictureNameOf(Weapons(), kWeaponSize, kWeaponCodes, kWeaponPicture, unknown_18);
}
EI_REPLACE_METHOD(0x006A27E0, CItemWeapon::PictureName);

mfc::CString CItemArmor::ModelName() { return ModelNameOf(Armors(), kArmorSize, kArmorCodes, unknown_18); }
EI_REPLACE_METHOD(0x006A3240, CItemArmor::ModelName);

mfc::CString CItemArmor::FirstListName() { return FirstListNameOf(Armors(), kArmorSize, unknown_18); }
EI_REPLACE_METHOD(0x006A36A0, CItemArmor::FirstListName);

mfc::CString CItemArmor::FigureName() { return HumanFigureName(this); }
EI_REPLACE_METHOD(0x006A3720, CItemArmor::FigureName);

mfc::CString CItemArmor::PictureName() {
    return PictureNameOf(Armors(), kArmorSize, kArmorCodes, kArmorPicture, unknown_18);
}
EI_REPLACE_METHOD(0x006A37B0, CItemArmor::PictureName);

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
    gamecrt::sprintf(number, Text(0x00796454), Field<int>(prototype, kRecordNumber));   // "%.2d"
    static const std::uintptr_t kBases[5] = {
        0x00796448,   // "basesword"
        0x00796440,   // "baseaxe"
        0x00796434,   // "basedagger"
        0x00796428,   // "basepike"
        0x0079641C,   // "baseclub"
    };
    auto copy = [](char* to, const char* from) { std::memcpy(to, from, std::strlen(from) + 1); };
    std::uint32_t type = static_cast<std::uint32_t>(Field<int>(prototype, field::kItemType));
    if (type < 5) {
        copy(name, Text(kBases[type]));
        copy(name + std::strlen(name), number);
    } else if (type == 5) {
        copy(name, Text(0x00796410));   // "basearrow00"
    } else if (type == 6) {
        copy(name, Text(0x00796404));   // "basearrow01"
    } else {
        copy(name, Text(0x007963F8));   // "baser_hand3"
    }
    *out = name;
    return true;
}
EI_REPLACE(0x006A0CC0, WeaponBaseFigure);

// --- Quick items, quest items, scrolls -----------------------------------------------------------

mfc::CString CItemQuick::FigureName() {
    mfc::CString name;
    const char* prototype = GetPrototype();
    if (prototype && gamecrt::mbsicmp(Field<const unsigned char*>(prototype, kQuickMaterial),
                                      reinterpret_cast<const unsigned char*>(kNoMaterial)) != 0) {
        const char* material = Materials().At(MaterialIndex(unknown_18), kMaterialSize);
        const char* materialName = material ? Field<const char*>(material, kMaterialName) : kUnknownMaterial;
        name.Format(kQuickMaterialFigure, Number(GetPrototype()), materialName);
        return name;
    }
    name.Format(kQuickFigure, Number(GetPrototype()));
    return name;
}
EI_REPLACE_METHOD(0x006A3CD0, CItemQuick::FigureName);

mfc::CString CItemQuick::PictureName() {
    mfc::CString name;
    name.Format(kQuickPicture, Number(GetPrototype()));
    return name;
}
EI_REPLACE_METHOD(0x006A4040, CItemQuick::PictureName);

mfc::CString CItemQuest::FigureName() {
    mfc::CString name;
    name.Format(kQuestFigure, Number(GetPrototype()));
    return name;
}
EI_REPLACE_METHOD(0x006A45C0, CItemQuest::FigureName);

mfc::CString CItemQuest::PictureName() {
    mfc::CString name;
    name.Format(kQuestPicture, Number(GetPrototype()));
    return name;
}
EI_REPLACE_METHOD(0x006A47B0, CItemQuest::PictureName);

mfc::CString CItemSpell::FigureName() {
    const auto* spell = static_cast<const CEngineObject*>(spell_);
    const char* prototype = Spells().At(PrototypeIndex(spell->unknown_18), kSpellSize);
    mfc::CString name;
    name.Format(kSpellFigure, Field<int>(prototype, kSpellFigureNumber));
    return name;
}
EI_REPLACE_METHOD(0x006A5820, CItemSpell::FigureName);

// --- Loot ----------------------------------------------------------------------------------------

namespace {
enum LootKind : std::uint32_t { kLootSelf = 0, kLootMaterial = 1, kLootRecord = 2 };
enum LootRecord : std::uint32_t { kRecordWeapon, kRecordArmor, kRecordQuick, kRecordSpell, kRecordModifier };
} // namespace

mfc::CString CItemLoot::FigureName() {
    mfc::CString name;
    if (kind_ == kLootRecord) {
        switch (recordType_) {
        case kRecordWeapon:
        case kRecordArmor: {
            bool weapon = recordType_ == kRecordWeapon;
            const char* record = weapon ? Weapons().At(index_, kWeaponSize) : Armors().At(index_, kArmorSize);
            mfc::CString code;
            int type = Field<int>(record, field::kItemType);
            if (type >= 0 && type < 7) code = (weapon ? kWeaponCodes : kArmorCodes)[type];
            name.Format(kRecordModelFormat, static_cast<const char*>(code), Field<int>(record, kRecordNumber),
                        Field<int>(record, kRecordVariant));
            break;
        }
        case kRecordQuick:
            name.Format(kQuickFigure, Field<int>(QuickItems().At(index_, kQuickItemSize), kRecordNumber));
            break;
        case kRecordSpell:
            name.Format(kPrototypeFigure, Field<int>(Spells().At(index_, kSpellSize), kSpellFigureNumber));
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
EI_REPLACE_METHOD(0x006A4B90, CItemLoot::FigureName);

mfc::CString CItemLoot::PictureName() {
    mfc::CString name;
    if (kind_ == kLootRecord) {
        switch (recordType_) {
        case kRecordWeapon:
        case kRecordArmor: {
            bool weapon = recordType_ == kRecordWeapon;
            const char* record = weapon ? Weapons().At(index_, kWeaponSize) : Armors().At(index_, kArmorSize);
            mfc::CString code;
            int type = Field<int>(record, field::kItemType);
            if (type >= 0 && type < 7) code = (weapon ? kWeaponCodes : kArmorCodes)[type];
            name.Format(weapon ? kWeaponPicture : kArmorPicture, static_cast<const char*>(code),
                        Field<int>(record, kRecordNumber));
            break;
        }
        case kRecordQuick:
            name.Format(kQuickPicture, Field<int>(QuickItems().At(index_, kQuickItemSize), kRecordNumber));
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
EI_REPLACE_METHOD(0x006A5030, CItemLoot::PictureName);
