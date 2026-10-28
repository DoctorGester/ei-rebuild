// CUnit::CollectItems: every item a unit holds and their spells as two lists, for the server controller's
// armour spell refresh. See unit.h.
#include "unit.h"

#include "item.h"

#include <list>

// The loop runs once when the item has a spell.
void GatherItem(std::list<CItem*>* items, std::list<CSpell*>* spells, CItem* item) {
    if (!item) return;
    items->push_back(item);
    CSpell* spell = item->spell_;
    if (spell) spells->push_back(spell);
}

namespace {
// Empty slots add nothing.
void AddItem(std::list<CItem*>* items, std::list<CSpell*>* spells, CItem* item) {
    GatherItem(items, spells, item);
}

template <typename Item>
void AddSlots(std::list<CItem*>* items, std::list<CSpell*>* spells, Item* const* slots, int count) {
    for (int i = 0; i < count; ++i) AddItem(items, spells, slots[i]);
}
} // namespace

void CUnit::CollectItems(std::list<CItem*>* items, std::list<CSpell*>* itemSpells) {
    items->clear();
    itemSpells->clear();
    AddSlots(items, itemSpells, weapons, 4);
    AddSlots(items, itemSpells, armor, 7);
    AddSlots(items, itemSpells, quickItems, 4);
    AddSlots(items, itemSpells, spells, 8);
    for (CItem* item : carried) AddItem(items, itemSpells, item);
}
