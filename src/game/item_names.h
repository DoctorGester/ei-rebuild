#pragma once

// Names built from the weapon and armour records by index: model names, the base figure and the figure parts a
// weapon is drawn with. The items' own name virtuals are in item.h; both are defined in item_names.cpp.

#include "item_db.h"

#include <cstdint>
#include <string>
#include <vector>

// The base figure (skeleton) a weapon prototype (by index) is animated with: "basesword", "baseaxe",
// "basedagger", "basepike", "baseclub" with the prototype's number as "%.2d" appended, "basearrow00" /
// "basearrow01" for types 5 / 6, else "baser_hand3". "" for an index out of range (kNoItemId, an empty weapon
// slot's id, among them).
std::string WeaponBaseFigure(std::uint32_t index);

// The model name "<type code>_<number>.<material name>.<variant>" of a weapon or armour prototype in a material
// (`codes` kWeaponCodes or kArmorCodes; "" as the code of a type without one); "" when either record is missing.
std::string RecordModelName(const itemdb::ItemRecordHead* prototype, const char* const (&codes)[7],
                            const itemdb::MaterialRecordHead* material);
// The same by armour / weapon prototype index and material index.
std::string ArmorModelName(std::uint32_t armorIndex, std::uint32_t materialIndex);
std::string WeaponModelName(std::uint32_t weaponIndex, std::uint32_t materialIndex);

// The names of the figure parts a weapon prototype (by index) is drawn with, replacing
// the contents of `out`: "rh3.sword07" for a sword with number 7, the bow and crossbow pieces plus
// the quiver and arrows. Nothing for an index out of range or an unknown weapon type.
void WeaponModelParts(std::uint32_t index, std::vector<std::string>* out);

// The same names for a packed item id, appended to a vector of strings.
void AppendWeaponModelParts(std::uint32_t packedId, std::vector<std::string>* out);
