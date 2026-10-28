// CUnit::CollectItems: every item a unit holds and their spells as two lists, for the server controller's
// armour spell refresh. See unit.h.
#include "unit.h"

#include "item.h"

// The loop runs once when the item has a spell.
void __fastcall GatherItem(mfc::CTypedPtrList<CItem>* items, mfc::CTypedPtrList<CSpell>* spells, CItem* item) {
    if (!item) return;
    items->AddTail(item);
    CSpell* spell = item->spell_;
    if (spell) spells->AddTail(spell);
}

namespace {
// Empty slots add nothing.
void AddItem(mfc::CTypedPtrList<CItem>* items, mfc::CTypedPtrList<CSpell>* spells, CItem* item) {
    GatherItem(items, spells, item);
}

template <typename Item>
void AddSlots(mfc::CTypedPtrList<CItem>* items, mfc::CTypedPtrList<CSpell>* spells, Item* const* slots, int count) {
    for (int i = 0; i < count; ++i) AddItem(items, spells, slots[i]);
}
} // namespace

void CUnit::CollectItems(mfc::CTypedPtrList<CItem>* items, mfc::CTypedPtrList<CSpell>* itemSpells) {
    items->RemoveAll();
    itemSpells->RemoveAll();
    AddSlots(items, itemSpells, weapons, 4);
    AddSlots(items, itemSpells, armor, 7);
    AddSlots(items, itemSpells, quickItems, 4);
    AddSlots(items, itemSpells, spells, 8);
    for (int i = 0; i < carriedCount; ++i) AddItem(items, itemSpells, carried[i]);
}
