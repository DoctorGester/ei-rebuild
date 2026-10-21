// CUnit::CollectItems (005316F0): every item a unit holds and their spells as two lists, for the
// server controller's armour spell refresh (00552B90's list, CUnitServer 005528F0). See unit.h.
#include "unit.h"

#include "item.h"

#include "ei/hook.h"

// 0069FB60 (A-H24; the original's loop runs once when the item has a spell).
void __fastcall GatherItem(mfc::CTypedPtrList<CEngineObject>* items, mfc::CTypedPtrList<void>* spells,
                           CEngineObject* item) {
    if (!item) return;
    items->AddTail(item);
    void* spell = static_cast<CItem*>(item)->spell_;
    if (spell) spells->AddTail(spell);
}
EI_REPLACE(0x0069FB60, GatherItem);

namespace {
// Empty slots add nothing.
void AddItem(mfc::CTypedPtrList<CEngineObject>* items, mfc::CTypedPtrList<void>* spells, CEngineObject* item) {
    GatherItem(items, spells, item);
}

void AddSlots(mfc::CTypedPtrList<CEngineObject>* items, mfc::CTypedPtrList<void>* spells, CItem* const* slots,
              int count) {
    for (int i = 0; i < count; ++i) AddItem(items, spells, slots[i]);
}
} // namespace

void CUnit::CollectItems(mfc::CTypedPtrList<CEngineObject>* items, mfc::CTypedPtrList<void>* itemSpells) {
    items->RemoveAll();
    itemSpells->RemoveAll();
    AddSlots(items, itemSpells, weapons, 4);
    AddSlots(items, itemSpells, armor, 7);
    AddSlots(items, itemSpells, quickItems, 4);
    AddSlots(items, itemSpells, spells, 8);
    for (int i = 0; i < carriedCount; ++i) AddItem(items, itemSpells, carried[i]);
}

EI_REPLACE_METHOD(0x005316F0, CUnit::CollectItems);
