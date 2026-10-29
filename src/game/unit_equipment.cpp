// A new unit's starting equipment from its prototype (the world adds zone units with it after
// ApplyPrototype). See unit_create.h (CMapObjectManager) and docs/notes/units.md ("Rewritten").
#include "item.h"
#include "item_manager.h"
#include "unit_create.h"
#include "game_strings.h"

#include "engine_object.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/crt.h"
#include "lib/str.h"

#include "world.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

// Through their game addresses (defined elsewhere; their verify wrappers stay in the path, and Rebirth's patches
// still run): the prototype table's name lookup (UnitPrototypeTable::FindByName, unit_create.cpp; inserts unknown
// names); the item manager's item by name (item_manager.cpp); FatalError (app.cpp); CUnit's EquipArmour /
// AddWeapon / AddSpell (unit_items.cpp, unit.cpp). The item manager's spell by name (with the world's manager
// context) is not defined here.

std::uint32_t Count(const std::vector<std::string>& names) { return static_cast<std::uint32_t>(names.size()); }

// A weapon name other than "none" (_mbscmp) and not empty.
bool Named(const std::string& name) {
    return str::Compare(name,
                           game::NoneText()) != 0 &&
           static_cast<int>(name.size()) != 0;
}

// The weapon named `name`; a FatalError message when it cannot be made (null is returned then).
Item* MakeWeapon(ItemManager* items, const std::string& name, const std::string& prototype) {
    Item* weapon = items->CreateItem(name);
    if (!weapon) game::FatalError(game::equipmenttexts::NoWeapon(), name, prototype);
    return weapon;
}
} // namespace

int CMapObjectManager::GiveStartingEquipment(CUnit* unit, const std::string& name) {
    CWorld* world = ContextAsWorld();
    CSpellManager* spellOwner = world->SpellManager();   // passed on to the spell factory
    ItemManager* items = world->ItemFactory();
    auto* prototype = g_unitDatabase.prototypes.FindByName(name);
    if (!prototype) {
        game::FatalError(game::equipmenttexts::NoPrototype(), name);
        return 0;
    }
    if (unit->MapType() == MapObjectType::Hero) {
        // The loop bounds are read again on every pass.
        for (std::uint32_t i = 0; i < Count(prototype->armourNames); ++i) {
            Item* armour = items->CreateItem(prototype->armourNames.begin()[i]);
            if (armour) unit->EquipArmour(armour);
            else game::FatalError(game::equipmenttexts::NoArmour(), prototype->armourNames.begin()[i], name);
        }
        if (Named(prototype->weaponName)) {
            // Added even when it could not be made: AddWeapon(null) returns 0, which becomes the slot.
            int slot = unit->AddWeapon(MakeWeapon(items, prototype->weaponName, name));
            if (slot != -1) unit->pendingWeapon = slot;
        }
        if (Named(prototype->secondWeaponName))
            unit->AddWeapon(MakeWeapon(items, prototype->secondWeaponName, name));
    }
    for (std::uint32_t i = 0; i < Count(prototype->spellNames); ++i) {
        Item* spell = items->CreateSpellItem(prototype->spellNames.begin()[i], spellOwner);
        if (spell) unit->AddSpell(spell);
        else game::FatalError(game::equipmenttexts::NoSpell(), prototype->spellNames.begin()[i], name);
    }
    return 1;
}
