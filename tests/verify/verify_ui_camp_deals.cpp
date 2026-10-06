// Checks for the camp's action buttons and the spell container taken apart (ui_camp_deals.cpp).
// They move items between parties, change the money and release objects, so ours run alone and
// the wrappers check the result: the money, the lists the page empties, the made item taken.
// Scenario ui_camp_deals (the item trade page: a quick item sold, then one taken back); the
// constructors' second pages and DeconstructSpell need a made spell / item (not reached).
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/party.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/world.h"

namespace {

void* s_unused;

int CountOf(void* list) { return list ? static_cast<CParty*>(list)->items_.size : 0; }
int Money() { return reinterpret_cast<CParty*>(LocalPlayerData())->Money(); }

// What a page's deal leaves empty (the lists it gives back), as counts.
struct Lists {
    int spell[3];
    int item[3];
    int spellResults, itemResults, extra;
};
Lists Snapshot(CCampPages* c) {
    Lists l = {};
    for (int i = 0; i < 3; ++i) {
        l.spell[i] = CountOf(c->spellParts_[i]);
        l.item[i] = CountOf(c->itemParts_[i]);
    }
    l.spellResults = CountOf(c->spellResults_);
    l.itemResults = CountOf(c->itemResults_);
    l.extra = CountOf(c->itemExtra_);
    return l;
}

} // namespace

// 00611220: the money drops by the cost (not on the trade page); the given-back lists are empty; a
// made spell / item is the only result and no longer held.
static void __fastcall CampDoActionVerify(CCampPages* self, void*) {
    int page = self->page_;
    int money = Money();
    int cost = self->cost_;
    bool madeSpell = self->spellMade_ && !self->deconstructing_;
    bool madeItem = self->itemMade_ && !self->deconstructing_;
    self->DoAction();
    Lists after = Snapshot(self);
    bool ok = self->page_ == page && Money() == (page == 1 ? money : money - cost);
    if (page == 2) ok = ok && after.spell[1] == 0 && after.spell[2] == 0;
    if (page == 4) ok = ok && after.item[1] == 0 && after.item[2] == 0;
    if (page == 6) ok = ok && after.extra == 0;
    if (page == 3 && madeSpell) ok = ok && !self->spellMade_ && after.spellResults == 1;
    if (page == 5 && madeItem) ok = ok && !self->itemMade_ && after.itemResults == 1;
    ei::verify::Check("CCampPages::DoAction", true, ok, "page %d money %d -> %d cost %d", page, money, Money(), cost);
}
EI_VERIFY(0x00611220, CampDoActionVerify, 0, s_unused);

// 00611760: the money is unchanged and the lists the page gives back are empty.
static void __fastcall CampUndoActionVerify(CCampPages* self, void*) {
    int page = self->page_;
    int money = Money();
    self->UndoAction();
    Lists after = Snapshot(self);
    bool ok = self->page_ == page && Money() == money;
    if (page == 2) ok = ok && after.spell[1] == 0 && after.spell[2] == 0;
    if (page == 3) ok = ok && after.spellResults == 0;
    if (page == 4) ok = ok && after.item[1] == 0 && after.item[2] == 0;
    if (page == 5) ok = ok && after.itemResults == 0;
    if (page == 6) ok = ok && after.extra == 0;
    ei::verify::Check("CCampPages::UndoAction", true, ok, "page %d money %d -> %d", page, money, Money());
}
EI_VERIFY(0x00611760, CampUndoActionVerify, 0, s_unused);

// 00612500: returns 1 and holds no made spell; the results gained the prototype and the runes.
static bool __fastcall CampDeconstructSpellVerify(CCampPages* self, void*) {
    int before = CountOf(self->spellResults_);
    bool result = self->DeconstructSpell();
    bool ok = result && !self->spellMade_ && CountOf(self->spellResults_) >= before;
    ei::verify::Check("CCampPages::DeconstructSpell", true, ok, "results %d -> %d", before,
                      CountOf(self->spellResults_));
    return result;
}
EI_VERIFY(0x00612500, CampDeconstructSpellVerify, 0, s_unused);

// 006129C0: returns 1 and holds no made item; the results lost the item and gained its parts (at
// least the instruction for a weapon, armour or quick item; creation may fail, so >= before - 1).
static bool __fastcall CampDeconstructItemVerify(CCampPages* self, void*) {
    int before = CountOf(self->itemResults_);
    bool result = self->DeconstructItem();
    bool ok = result && !self->itemMade_ && CountOf(self->itemResults_) >= before - 1;
    ei::verify::Check("CCampPages::DeconstructItem", true, ok, "results %d -> %d", before, CountOf(self->itemResults_));
    return result;
}
EI_VERIFY(0x006129C0, CampDeconstructItemVerify, 0, s_unused);