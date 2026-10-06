// Checks for unit_item_lists.cpp (A-U4).
// - CollectItems (005316F0): output. The original fills two scratch lists (empty, block size 10,
//   the callers' vtables), ours the caller's; both item sequences and spell sequences compared,
//   then the scratch nodes are freed.
// Reached by: CUnitServer 005528F0 (the armour spell refresh) from controller slots 25 (weapon
// switch, 00553610) and 29 (equipment refresh, 005535F0): equipped units on zone load and in
// combat (units, combat, unit_create).
#include "verify.h"

#include "../../src/game/unit.h"

#include <vector>

namespace {
template <typename T>
std::vector<const void*> Items(const mfc::CTypedPtrList<T>& list) {
    std::vector<const void*> out;
    for (auto* node = list.head; node; node = node->next) out.push_back(node->data);
    return out;
}

template <typename T>
void MakeScratch(mfc::CTypedPtrList<T>& scratch, const mfc::CTypedPtrList<T>& like) {
    scratch = {};
    scratch.vtable = like.vtable;
    scratch.blockSize = like.blockSize;
}
} // namespace

// 005316F0: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI (5 bytes).
static void(__fastcall* s_collectItems)(CUnit*, void*, mfc::CTypedPtrList<CEngineObject>*, mfc::CTypedPtrList<void>*);
static void __fastcall CollectItemsVerify(CUnit* self, void*, mfc::CTypedPtrList<CEngineObject>* items,
                                          mfc::CTypedPtrList<void>* spells) {
    mfc::CTypedPtrList<CEngineObject> scratchItems;
    mfc::CTypedPtrList<void> scratchSpells;
    MakeScratch(scratchItems, *items);
    MakeScratch(scratchSpells, *spells);
    s_collectItems(self, nullptr, &scratchItems, &scratchSpells);
    self->CollectItems(items, spells);
    bool ok = Items(scratchItems) == Items(*items) && Items(scratchSpells) == Items(*spells) &&
              scratchItems.count == items->count && scratchSpells.count == spells->count;
    ei::verify::Check("CUnit::CollectItems", true, ok, "%d / %d items, %d / %d spells", scratchItems.count, items->count,
                      scratchSpells.count, spells->count);
    scratchItems.RemoveAll();
    scratchSpells.RemoveAll();
}
EI_VERIFY(0x005316F0, CollectItemsVerify, 5, s_collectItems);
