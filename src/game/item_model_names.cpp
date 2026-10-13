// The armour and weapon model names by record indices (006A3480, 006A0AA0; unit skins and models
// build the texture of a worn piece with them) and the weapon's from its records (006A0BF0). See
// item.h; the item classes' own name builders are item_names.cpp.
#include "item.h"

#include "ei/hook.h"
#include "lib/mfc.h"

#include <cstring>

using namespace itemdb;

namespace {
// The two-letter armour type codes (00796534..0079654C), by type.
constexpr const char* kArmorCodes[7] = {"hl", "pl", "lg", "sh", "pt", "bt", "gl"};
// The weapons' (007963E0..007963F4, "hm" at 00784524).
constexpr const char* kWeaponCodes[7] = {"sw", "ax", "dg", "sp", "hm", "bw", "cb"};
} // namespace

void __fastcall ArmorModelName(mfc::CString* out, std::uint32_t armorIndex, std::uint32_t materialIndex) {
    auto* material = reinterpret_cast<const MaterialRecordHead*>(Materials().At(materialIndex, kMaterialSize));
    auto* armor = reinterpret_cast<const ItemRecordHead*>(Armors().At(armorIndex, kArmorSize));
    out->Empty();
    if (!material || !armor) return;
    // The code goes into the stack slot of `materialIndex` (4 bytes): another type keeps the
    // index's bytes there.
    char code[4];
    std::memcpy(code, &materialIndex, sizeof(code));
    if (static_cast<std::uint32_t>(armor->type) <= 6) std::memcpy(code, kArmorCodes[armor->type], 3);
    out->Format("%s_%.2d.%s.%d", code, armor->number, material->name, armor->variant);
}
EI_REPLACE(0x006A3480, ArmorModelName);

void __fastcall WeaponModelName(mfc::CString* out, std::uint32_t weaponIndex, std::uint32_t materialIndex) {
    auto* material = reinterpret_cast<const MaterialRecordHead*>(Materials().At(materialIndex, kMaterialSize));
    auto* weapon = reinterpret_cast<const ItemRecordHead*>(Weapons().At(weaponIndex, kWeaponSize));
    out->Empty();
    if (!weapon || !material) return;
    // As the armour's: the code goes into the stack slot of `materialIndex`.
    char code[4];
    std::memcpy(code, &materialIndex, sizeof(code));
    if (static_cast<std::uint32_t>(weapon->type) <= 6) std::memcpy(code, kWeaponCodes[weapon->type], 3);
    out->Format("%s_%.2d.%s.%d", code, weapon->number, material->name, weapon->variant);
}
EI_REPLACE(0x006A0AA0, WeaponModelName);

void __fastcall WeaponRecordModelName(mfc::CString* out, const WeaponRecord* weapon, const MaterialRecordHead* material) {
    // The code's four bytes are the stack slot the original saved ECX (`out`) in.
    char code[4];
    std::memcpy(code, &out, sizeof(code));
    if (static_cast<std::uint32_t>(weapon->type) <= 6) std::memcpy(code, kWeaponCodes[weapon->type], 3);
    out->Format("%s_%.2d.%s.%d", code, weapon->number, material->name, weapon->variant);
}
EI_REPLACE(0x006A0BF0, WeaponRecordModelName);
