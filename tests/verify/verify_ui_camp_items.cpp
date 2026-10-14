// Checks for the camp screens' item helpers: the predicates 005EE050-005EE130 (ui_screens.cpp),
// the moves 005EE1B0 / 005EE2C0 (ui_item_screen.cpp) and the item strip's ScrollToItem 00621570
// (ui_screens.cpp). Scenarios ui_camp and ui_describe reach them (the camp's pages, MoveItem,
// PutOn / TakeOff, the strips' tabs).
//
// The predicates run side by side with the original (pure). The moves change the party lists and
// make copies, so only ours runs: the result must be held by the target list, the source must no
// longer hold a moved whole stack, a refused move changes nothing. ScrollToItem (ours only) must
// leave the visible list exactly as the rebuild rule gives it for the tab it ends on, the scroll
// stopped and, when the item is listed, its last match within the visible seven slots.
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/party.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/game/ui_screens.h"

#include <vector>

namespace {

// --- Predicates ----------------------------------------------------------------------------------

using Predicate = bool(__fastcall*)(UiItemKinds*, void*, const char*);
Predicate s_isSpell, s_isModifier, s_isPart, s_isKindOne, s_isBuildable;
BOOL(__fastcall* s_isSpellContainer)(UiItemKinds*, void*, const char*);

void* s_unused;

bool Contains(CParty* list, CItem* item) {
    for (int i = 0; i < list->items_.size; ++i)
        if (list->items_.data[i] == item) return true;
    return false;
}

} // namespace

// Stolen bytes: MOV EAX,[ESP+4] (4) + CMP dword [EAX+0x10],imm32 (7).
static bool __fastcall IsSpellVerify(UiItemKinds* self, void*, const char* item) {
    bool expected = s_isSpell(self, nullptr, item);
    ei::verify::Check("UiItemKinds::IsSpell", expected, self->UiItemKinds::IsSpell(item), "item %p", item);
    return expected;
}
EI_VERIFY(0x005EE050, IsSpellVerify, 11, s_isSpell);

static bool __fastcall IsModifierVerify(UiItemKinds* self, void*, const char* item) {
    bool expected = s_isModifier(self, nullptr, item);
    ei::verify::Check("UiItemKinds::IsModifier", expected, self->UiItemKinds::IsModifier(item), "item %p", item);
    return expected;
}
EI_VERIFY(0x005EE080, IsModifierVerify, 11, s_isModifier);

static bool __fastcall IsPartVerify(UiItemKinds* self, void*, const char* item) {
    bool expected = s_isPart(self, nullptr, item);
    ei::verify::Check("UiItemKinds::IsPart", expected, self->UiItemKinds::IsPart(item), "item %p", item);
    return expected;
}
EI_VERIFY(0x005EE0D0, IsPartVerify, 11, s_isPart);

static bool __fastcall IsKindOneVerify(UiItemKinds* self, void*, const char* item) {
    bool expected = s_isKindOne(self, nullptr, item);
    ei::verify::Check("UiItemKinds::IsKindOne", expected, self->UiItemKinds::IsKindOne(item), "item %p", item);
    return expected;
}
EI_VERIFY(0x005EE110, IsKindOneVerify, 11, s_isKindOne);

// MOV ECX,[ESP+4] (4) + XOR EAX,EAX (2).
static BOOL __fastcall IsSpellContainerVerify(UiItemKinds* self, void*, const char* item) {
    BOOL expected = s_isSpellContainer(self, nullptr, item);
    ei::verify::Check("UiItemKinds::IsSpellContainer", expected, self->UiItemKinds::IsSpellContainer(item),
                      "item %p", item);
    return expected;
}
EI_VERIFY(0x005EE0B0, IsSpellContainerVerify, 6, s_isSpellContainer);

// PUSH ESI (1) + MOV ESI,[ESP+8] (4) + MOV ECX,ESI (2).
static bool __fastcall IsBuildableVerify(UiItemKinds* self, void*, const char* item) {
    bool expected = s_isBuildable(self, nullptr, item);
    ei::verify::Check("UiItemKinds::IsBuildable", expected, self->UiItemKinds::IsBuildable(item), "item %p", item);
    return expected;
}
EI_VERIFY(0x005EE130, IsBuildableVerify, 7, s_isBuildable);

// --- Moves (ours only) ------------------------------------------------------------------------------

static CItem* __fastcall MoveItemVerify(CItemScreen* self, void*, CItem* item, CParty* to, CParty* from, int limit,
                                        int merge, int turning) {
    int toBefore = to->items_.size;
    int fromBefore = from->items_.size;
    bool searched = limit > 0 && toBefore >= limit;
    bool match = false;
    for (int i = 0; searched && i < limit && !match; ++i)
        match = item->IsSameAsSlot(static_cast<CItem*>(to->items_.data[i])) != 0;
    bool whole = static_cast<std::uint8_t>(merge) != 0 && self->interface_->shiftDown_ != 0;
    // A stack of more than one stays in `from` (one fewer) unless the whole of it moves.
    bool stays = !whole && static_cast<int>(item->count_) > 1;
    CItem* moved = self->CItemScreen::MoveItem(item, to, from, limit, merge, turning);
    bool ok;
    if (searched && !match) {
        ok = !moved && to->items_.size == toBefore && from->items_.size == fromBefore;
    } else {
        ok = moved && Contains(to, moved) && (stays || !Contains(from, item));
        if (moved && !searched && static_cast<std::uint8_t>(turning)) ok &= ItemTurnOf(moved) && ItemTurnOf(moved)->flag;
    }
    ei::verify::Check("CItemScreen::MoveItem", true, ok, "item %p limit %d merge %d turning %d to %d->%d from %d->%d",
                      item, limit, merge, turning, toBefore, to->items_.size, fromBefore, from->items_.size);
    return moved;
}
EI_VERIFY(0x005EE1B0, MoveItemVerify, 0, s_unused);

// `to` is only read when `from` holds something, as the original does: leaving the camp without a
// constructor returns the empty parts lists to the constructor's lists, which are null (X13).
static CItem* __fastcall ReturnAllVerify(CItemScreen* self, void*, CParty* to, CParty* from) {
    int fromBefore = from->items_.size;
    CItem* moved = self->CItemScreen::ReturnAll(to, from);
    bool ok = from->items_.size == 0 && (fromBefore == 0 ? !moved : moved && Contains(to, moved));
    ei::verify::Check("CItemScreen::ReturnAll", true, ok, "from %d to %d", fromBefore,
                      fromBefore ? to->items_.size : -1);
    return moved;
}
EI_VERIFY(0x005EE2C0, ReturnAllVerify, 0, s_unused);

// --- Item strip ---------------------------------------------------------------------------------

static void __fastcall ScrollToItemVerify(CUiItemStrip* self, void*, char* item) {
    float pending = self->pending_;
    float velocity = self->velocity_;
    int count = self->count_;
    self->CUiItemStrip::ScrollToItem(item);
    if (!item) {
        ei::verify::Check("CUiItemStrip::ScrollToItem", true,
                          self->pending_ == pending && self->velocity_ == velocity, "null item");
        return;
    }
    // The list the rebuild rule gives for the tab the strip is on now.
    std::vector<char*> expected;
    bool all = self->filter_ == 0 && self->tab_ == 5;
    for (int i = 0; i < self->source_->size; ++i) {
        char* entry = self->source_->data[i];
        if (all || self->CUiItemStrip::TabOf(entry) == self->tab_) expected.push_back(entry);
    }
    bool ok = self->list_.size == static_cast<int>(expected.size()) && self->pending_ == 0.0f &&
              self->velocity_ == 0.0f && self->count_ == count;
    int found = -1;
    for (int i = 0; ok && i < self->list_.size; ++i) {
        ok &= self->list_.data[i] == expected[i];
        if (reinterpret_cast<CItem*>(item)->IsSameAsSlot(reinterpret_cast<CItem*>(expected[i]))) found = i;
    }
    float position = self->position_[self->tab_];
    if (ok && found >= 0) ok = position >= static_cast<float>(found - 6) && position <= static_cast<float>(found);
    ei::verify::Check("CUiItemStrip::ScrollToItem", true, ok, "tab %d filter %d list %d found %d position %g",
                      self->tab_, self->filter_, self->list_.size, found, position);
}
EI_VERIFY(0x00621570, ScrollToItemVerify, 0, s_unused);
