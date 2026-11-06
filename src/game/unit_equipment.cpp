// A new unit's starting equipment from its prototype (the world adds zone units with it after
// ApplyPrototype). See unit_create.h (MapObjectManager) and docs/notes/units.md ("Creation helpers").
#include "game/item.h"
#include "game/item_manager.h"
#include "game/unit_create.h"

#include "game/core/engine_object.h"
#include "game/unit.h"
#include "game/unit_db.h"

#include "lib/crt.h"
#include "lib/str.h"

#include "game/world/world.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

// Through their game addresses (defined elsewhere; their verify wrappers stay in the path, and Rebirth's patches
// still run): the prototype table's name lookup (UnitPrototypeTable::FindByName, unit_create.cpp; inserts unknown
// names); the item manager's item by name (item_manager.cpp); Unit's EquipArmour /
// AddWeapon / AddSpell (unit_items.cpp, unit.cpp). The item manager's spell by name (with the world's manager
// context) is not defined here.

std::uint32_t Count(const std::vector<std::string>& names) { return static_cast<std::uint32_t>(names.size()); }

// A weapon name other than "none" (_mbscmp) and not empty.
bool Named(const std::string& name) {
    return str::Compare(name,
                           "none") != 0 &&
           static_cast<int>(name.size()) != 0;
}

// The weapon named `name`; a warning when it cannot be made (null is returned then).
Item* MakeWeapon(ItemManager* items, const std::string& name, const std::string& prototype) {
    Item* weapon = items->CreateItem(name);
    if (!weapon) game::Warning("Can't create weapon \"%s\" for unit \"%s\"", name, prototype);
    return weapon;
}
} // namespace

int MapObjectManager::GiveStartingEquipment(Unit* unit, const std::string& name) {
    SpellManager* spellOwner = context->GetSpellManager();   // passed on to the spell factory
    ItemManager* items = context->ItemFactory();
    auto* prototype = g_unitDatabase.prototypes.FindByName(name);
    if (!prototype) {   // a map's unit of a prototype the units database lacks (Lost in Astral's gz8g, gz17g)
        game::Warning("Can't find prototype \"%s\" for unit %d: no starting equipment", name, unit->id);
        return 0;
    }
    if (unit->MapType() == MapObjectType::Hero) {
        // The loop bounds are read again on every pass.
        for (std::uint32_t i = 0; i < Count(prototype->armourNames); ++i) {
            Item* armour = items->CreateItem(prototype->armourNames.begin()[i]);
            if (armour) unit->EquipArmour(armour);
            else game::Warning("Can't create armor \"%s\" for unit \"%s\"", prototype->armourNames.begin()[i], name);
        }
        if (Named(prototype->weaponName)) {
            int slot = unit->AddWeapon(MakeWeapon(items, prototype->weaponName, name));
            if (slot != -1) unit->pendingWeapon = slot;
        }
        if (Named(prototype->secondWeaponName))
            unit->AddWeapon(MakeWeapon(items, prototype->secondWeaponName, name));
    }
    for (std::uint32_t i = 0; i < Count(prototype->spellNames); ++i) {
        Item* spell = items->CreateSpellItem(prototype->spellNames.begin()[i], spellOwner);
        if (spell) unit->AddSpell(spell);
        else game::Warning("Can't create spell \"%s\" for unit \"%s\"", prototype->spellNames.begin()[i], name);
    }
    return 1;
}
