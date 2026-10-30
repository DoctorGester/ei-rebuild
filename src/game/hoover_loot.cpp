// The loot of a dead unit (the server's world calls it on its Hoover, +0x140, which the function does not
// use): the prototype's items (new objects from the item manager, put on both lists), the gold (two rolls of
// the prototype: an amount between a minimum and a maximum, kept when a percent roll is within the chance),
// the quick items and carried items, and for units with a text key also the armour and weapons, all taken off
// the unit and put on the drop list. A unit is looted once (flag 0x100000). Plain float maths for the amounts
// (rule 7). See docs/notes/objects.md ("Rewritten").
#include "item.h"
#include "item_manager.h"
#include "object.h"
#include "object_tables.h"
#include "random.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"
#include "lib/rounding.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// `object` appended with a new reference.
void Append(std::vector<Item*>* list, Item* object) {
    object->AddRef();
    list->push_back(object);
}

// One gold roll of the prototype: the amount (the minimum rounded, or a random value between the minimum and
// a larger maximum, truncated), kept when the percent roll (CRT rand) is within the chance. The spread times the
// random word is rounded once; the original's scale 1 / (2^32 - 1) is the double 2^-32 (1 + 2^-32), and a float
// times that rounds to the float times 2^-32.
void RollGold(const UnitGoldRoll& roll, int* gold) {
    float low = roll.minimum;
    float high = roll.maximum;
    int amount = lib::RoundEven(low);
    if (high > low) amount = static_cast<int>(pc24::Mul(high - low, g_random.Next()) * 0x1p-32f + low);
    int percent = libcrt::RandBelow(100) + 1;
    EI_NAN_AUDIT(roll.chance);
    if (static_cast<float>(percent) <= roll.chance) *gold += amount;
}

} // namespace

int Hoover::CollectLoot(World* world, Unit* cunit, std::vector<Item*>* drops, std::vector<Item*>* items, int* gold) {
    bool keyed = IsTextKey(cunit->id);
    if (cunit->flags & MapObjectFlag::LootTaken) return 0;
    ItemManager* manager = world->ItemFactory();
    const UnitPrototypeRecord* prototype = cunit->GetPrototype();
    *gold = 0;
    if (prototype) {
        const std::vector<std::string>& itemNames = prototype->lootItems;
        for (std::size_t i = 0; i < itemNames.size(); ++i) {   // the end is read again each time
            Item* item = manager->CreateItem(itemNames[i]);
            if (!item) continue;
            Append(drops, item);
            items->push_back(item);   // without a new reference
        }
        const UnitPrototypeRecord* record = prototype;
        RollGold(record->gold, gold);
        RollGold(record->secondGold, gold);
        if (*gold < 0) *gold = 0;
    }
    for (int slot = 0; slot < 4; ++slot) {
        if (Item* item = cunit->GetQuickItem(slot)) Append(drops, item);
        cunit->RemoveQuickItem(slot);
    }
    for (Item* item : cunit->carried) {
        if (item) Append(drops, item);
    }
    while (!cunit->carried.empty()) cunit->RemoveCarried(0);
    if (keyed) {
        for (int slot = 0; slot < 7; ++slot) {
            ItemArmor* armour = cunit->WornItem(slot);
            if (!armour) continue;
            Append(drops, armour);
            cunit->UnequipArmour(armour->GetType());
        }
        for (int slot = 0; slot < 4; ++slot) {
            ItemWeapon* weapon = cunit->GetWeapon(slot);
            if (!weapon) continue;
            Append(drops, weapon);
            cunit->RemoveWeapon(slot);
        }
    }
    cunit->flags |= MapObjectFlag::LootTaken;
    return 1;
}
