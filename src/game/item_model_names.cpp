// The armour and weapon model names by record indices (unit skins and models build the texture of a worn piece with
// them) and the weapon's from its records. See item.h; the item classes' own name builders are item_names.cpp.
#include "item.h"

#include <cstdint>
#include <cstring>
#include <string>
#include "lib/str.h"


using namespace itemdb;

void ArmorModelName(std::string* out, std::uint32_t armorIndex, std::uint32_t materialIndex) {
    const MaterialRecordHead* material = MaterialAt(materialIndex);
    const ArmorRecord* armor = ArmorAt(armorIndex);
    out->clear();
    if (!material || !armor) return;
    // The code goes into the storage of `materialIndex` (4 bytes): another type keeps the
    // index's bytes there.
    char code[4];
    std::memcpy(code, &materialIndex, sizeof(code));
    if (static_cast<std::uint32_t>(armor->type) <= 6) std::memcpy(code, kArmorCodes[armor->type], 3);
    *out = str::Format("%s_%.2d.%s.%d", code, armor->number, material->name.c_str(), armor->variant);
}

void WeaponModelName(std::string* out, std::uint32_t weaponIndex, std::uint32_t materialIndex) {
    const MaterialRecordHead* material = MaterialAt(materialIndex);
    const WeaponRecord* weapon = WeaponAt(weaponIndex);
    out->clear();
    if (!weapon || !material) return;
    // As the armour's: the code goes into the storage of `materialIndex`.
    char code[4];
    std::memcpy(code, &materialIndex, sizeof(code));
    if (static_cast<std::uint32_t>(weapon->type) <= 6) std::memcpy(code, kWeaponCodes[weapon->type], 3);
    *out = str::Format("%s_%.2d.%s.%d", code, weapon->number, material->name.c_str(), weapon->variant);
}

void WeaponRecordModelName(std::string* out, const WeaponRecord* weapon, const MaterialRecordHead* material) {
    // The code's four bytes are the stack memory `out` was saved in.
    char code[4];
    std::memcpy(code, &out, sizeof(code));
    if (static_cast<std::uint32_t>(weapon->type) <= 6) std::memcpy(code, kWeaponCodes[weapon->type], 3);
    *out = str::Format("%s_%.2d.%s.%d", code, weapon->number, material->name.c_str(), weapon->variant);
}
