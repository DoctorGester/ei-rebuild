// CUnit::CollectItems: every item a unit holds and their spells as two lists, for the server controller's
// armour spell refresh. See unit.h.
#include "unit.h"

#include "item.h"

#include <vector>

void ItemsAndSpells::Add(Item* item) {
    if (!item) return;
    items.push_back(item);
    if (CSpell* spell = item->spell) spells.push_back(spell);
}

namespace {
template <typename Item>
void AddSlots(ItemsAndSpells& held, Item* const* slots, int count) {
    for (int i = 0; i < count; ++i) held.Add(slots[i]);
}
} // namespace

ItemsAndSpells CUnit::CollectItems() {
    ItemsAndSpells held;
    AddSlots(held, weapons, 4);
    AddSlots(held, armor, 7);
    AddSlots(held, quickItems, 4);
    AddSlots(held, spells, 8);
    for (Item* item : carried) held.Add(item);
    return held;
}
