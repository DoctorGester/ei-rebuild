// CUnit::CollectItems: every item a unit holds and their spells as two lists, for the server controller's
// armour spell refresh. See unit.h.
#include "unit.h"

#include "item.h"

#include <list>

void ItemsAndSpells::Add(CItem* item) {
    if (!item) return;
    items.push_back(item);
    if (CSpell* spell = item->spell_) spells.push_back(spell);
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
    for (CItem* item : carried) held.Add(item);
    return held;
}
