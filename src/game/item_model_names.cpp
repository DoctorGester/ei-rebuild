// The weapon and armour model names (unit skins and models build the texture of a worn piece with them; the
// items' ModelName). See item.h; the item classes' other name builders are item_names.cpp.
#include "item.h"

#include "lib/str.h"

#include <cstdint>
#include <string>

using namespace itemdb;

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
