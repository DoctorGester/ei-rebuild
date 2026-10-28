// The loot of a dead unit (the server's world calls it on its CHoover, +0x140, which the function does not
// use): the prototype's items (new objects from the item manager, put on both lists), the gold (two rolls of
// the prototype: an amount between a minimum and a maximum, kept when a percent roll is within the chance),
// the quick items and carried items, and for units with a text key also the armour and weapons, all taken off
// the unit and put on the drop list. A unit is looted once (flag 0x100000). Plain float maths for the amounts
// (rule 7). See docs/notes/objects.md ("Rewritten").
#include "item.h"
#include "object.h"
#include "object_tables.h"
#include "random.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include <list>
#include <vector>
#include <cstddef>
#include <string>
#include "lib/nan_audit.h"

namespace {

// A list's NewNode(prev, next) (data cleared). The armour and weapons inline it (with the same effects).

void Append(std::list<CItem*>* list, CItem* object) {
    ++object->refCount_;
    auto node = list->emplace(list->end());
    *node = object;
}
// The node appended without a new reference (the prototype items on the second list).
void AppendShared(std::list<CItem*>* list, CItem* object) {
    auto node = list->emplace(list->end());
    *node = object;
}

// One gold roll of the prototype: the amount (the minimum rounded, or a random value between the minimum and
// a larger maximum, truncated), kept when the percent roll (CRT rand) is within the chance.
void RollGold(const UnitGoldRoll& roll, int* gold) {
    float low = roll.minimum;
    float high = roll.maximum;
    int amount = x87::fistp(low);
    if (high > low) {
        double random = x87::FromUnsigned(g_random.Next());
        const double scale = 2.3283064370807974e-10;   // 1 / 0xFFFFFFFF
        amount = static_cast<int>(static_cast<double>(high - low) * random * scale + low);
    }
    int percent = libcrt::Rand() * 100 / 0x7FFF % 100 + 1;
    EI_NAN_AUDIT(roll.chance);
    if (static_cast<float>(percent) <= roll.chance) *gold += amount;
}

} // namespace

int CHoover::CollectLoot(CWorld* world, CUnit* cunit, std::list<CItem*>* drops, std::list<CItem*>* items, int* gold) {
    bool keyed = IsTextKey(cunit->id_) & 0xFF;
    if (cunit->flags_ & 0x100000) return 0;
    CItemManager* manager = world->ItemFactory();
    const UnitPrototypeRecord* prototype = cunit->GetPrototype();
    *gold = 0;
    if (prototype) {
        const std::vector<std::string>& itemNames = prototype->lootItems;
        for (std::size_t i = 0; i < itemNames.size(); ++i) {   // the end is read again each time
            CItem* item = manager->CreateItem(itemNames[i]);
            if (!item) continue;
            Append(drops, item);
            AppendShared(items, item);
        }
        const UnitPrototypeRecord* record = prototype;
        RollGold(record->gold, gold);
        RollGold(record->secondGold, gold);
        if (*gold < 0) *gold = 0;
    }
    for (int slot = 0; slot < 4; ++slot) {
        if (CItem* item = cunit->GetQuickItem(slot)) Append(drops, item);
        cunit->RemoveQuickItem(slot);
    }
    for (CItem* item : cunit->carried) {
        if (item) Append(drops, item);
    }
    while (!cunit->carried.empty()) cunit->RemoveCarried(0);
    if (keyed) {
        for (int slot = 0; slot < 7; ++slot) {
            CItemArmor* armour = cunit->WornItem(slot);
            if (!armour) continue;
            Append(drops, armour);
            cunit->UnequipArmour(armour->GetType());
        }
        for (int slot = 0; slot < 4; ++slot) {
            CItemWeapon* weapon = cunit->GetWeapon(slot);
            if (!weapon) continue;
            Append(drops, weapon);
            cunit->RemoveWeapon(slot);
        }
    }
    cunit->flags_ |= 0x100000;
    return 1;
}
