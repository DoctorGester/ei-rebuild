// Checks for the hero panels' small methods (ui_equip_panels.cpp).
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/unit.h"

namespace {

void* s_unused;

// Whether `index` is `item`'s first place among `count` entries of `list` (or -1 and absent).
bool FirstPlace(CEngineObject* const* list, int count, const CItem* item, int index) {
    for (int i = 0; i < count; ++i)
        if (list[i] == item) return index == i;
    return index == -1;
}

} // namespace

// 006229E0 / 00622A30 / 00622BC0, ours only: the index written is the item's first place in its
// list, or -1. Reached by the hero screen (ShowHero, the statistics page's take-back) and the camp.
static void __fastcall EquipShowWeaponVerify(CHeroEquipPanel* self, void*, CItem* weapon) {
    self->ShowWeapon(weapon);
    PartyUnitRecord* hero = ShownHero(self->owner_);
    ei::verify::Check("CHeroEquipPanel::ShowWeapon", true,
                      FirstPlace(hero->weapons, static_cast<int>(hero->weaponCount), weapon,
                                 static_cast<int>(hero->weaponIndex)),
                      "index %d of %u", static_cast<int>(hero->weaponIndex), hero->weaponCount);
}
EI_VERIFY(0x006229E0, EquipShowWeaponVerify, 0, s_unused);

static void __fastcall EquipShowArmourVerify(CHeroEquipPanel* self, void*, CItem* item) {
    self->ShowArmour(item);
    PartyUnitRecord* hero = ShownHero(self->owner_);
    ei::verify::Check("CHeroEquipPanel::ShowArmour", true,
                      FirstPlace(hero->quickItems, static_cast<int>(hero->quickItemCount), item, self->selected_),
                      "index %d of %u", self->selected_, hero->quickItemCount);
}
EI_VERIFY(0x00622A30, EquipShowArmourVerify, 0, s_unused);

static void __fastcall QuickStripShowEntryVerify(CHeroQuickStrip* self, void*, CItem* item) {
    self->ShowEntry(item);
    PartyUnitRecord* hero = ShownHero(self->owner_);
    ei::verify::Check("CHeroQuickStrip::ShowEntry", true,
                      FirstPlace(hero->spells, static_cast<int>(hero->spellCount), item, self->selected_),
                      "index %d of %u", self->selected_, hero->spellCount);
}
EI_VERIFY(0x00622BC0, QuickStripShowEntryVerify, 0, s_unused);

// 00622960, ours only (it plays a sound and refreshes the owner): a right click on a filled weapon
// slot of the body (regions 0-3) shows that weapon. ui_camp's right clicks reach it when they land
// on the panel.
static void __fastcall EquipRightDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CHeroEquipPanel>(input);
    bool active = self->body_ && self->enabled_;
    int region = active ? self->RegionAt(x, y) : -1;
    self->OnRightDown(buttons, x, y);
    if (region < 0 || region >= 4) return;
    PartyUnitRecord* hero = ShownHero(self->owner_);
    if (region >= static_cast<int>(hero->weaponCount)) return;
    ei::verify::Check("CHeroEquipPanel::OnRightDown", region, static_cast<int>(hero->weaponIndex), "weapon slot");
}
EI_VERIFY(0x00622960, EquipRightDownVerify, 0, s_unused);

// 00622860 / 00622D40: side by side (they only read): the original first with a scratch topic,
// then ours; the item and the topic compared. Reached by the camp's and the hero screen's hover
// (Draw asks every panel for the item under the mouse): ui_camp, ui_describe, ui_hero_screen.
static CItem*(__fastcall* s_equipItemAt)(CHeroEquipPanel*, void*, int, int, int*);
static CItem* __fastcall EquipItemAtVerify(CHeroEquipPanel* self, void*, int x, int y, int* topic) {
    int expectedTopic = -2;
    CItem* expected = s_equipItemAt(self, nullptr, x, y, &expectedTopic);
    CItem* actual = self->ItemAt(x, y, topic);
    ei::verify::Check("CHeroEquipPanel::ItemAt", true, expected == actual && expectedTopic == *topic,
                      "(%d, %d): %p topic %d, ours %p topic %d", x, y, static_cast<void*>(expected), expectedTopic,
                      static_cast<void*>(actual), *topic);
    return actual;
}
EI_VERIFY(0x00622860, EquipItemAtVerify, 6, s_equipItemAt);

static CItem*(__fastcall* s_stripItemAt)(CHeroQuickStrip*, void*, int, int, int*);
static CItem* __fastcall QuickStripItemAtVerify(CHeroQuickStrip* self, void*, int x, int y, int* topic) {
    int expectedTopic = -2;
    CItem* expected = s_stripItemAt(self, nullptr, x, y, &expectedTopic);
    CItem* actual = self->ItemAt(x, y, topic);
    ei::verify::Check("CHeroQuickStrip::ItemAt", true, expected == actual && expectedTopic == *topic,
                      "(%d, %d): %p topic %d, ours %p topic %d", x, y, static_cast<void*>(expected), expectedTopic,
                      static_cast<void*>(actual), *topic);
    return actual;
}
EI_VERIFY(0x00622D40, QuickStripItemAtVerify, 5, s_stripItemAt);

// 00627920 / 00628060 (the trade and repair grids' slot 25, A-I18): side by side as above.
// Reached by the camp's hover over a constructor's first page or the repair page (ui_camp,
// ui_camp_deals, trader_stock: the camp's Draw asks the shown panels for the item under the mouse).
static CItem*(__fastcall* s_tradeGridItemAt)(CTradeGrid*, void*, int, int, int*);
static CItem* __fastcall TradeGridItemAtVerify(CTradeGrid* self, void*, int x, int y, int* topic) {
    int expectedTopic = -2;
    CItem* expected = s_tradeGridItemAt(self, nullptr, x, y, &expectedTopic);
    CItem* actual = self->ItemAt(x, y, topic);
    ei::verify::Check("CTradeGrid::ItemAt", true, expected == actual && expectedTopic == *topic,
                      "(%d, %d): %p topic %d, ours %p topic %d", x, y, static_cast<void*>(expected), expectedTopic,
                      static_cast<void*>(actual), *topic);
    return actual;
}
EI_VERIFY(0x00627920, TradeGridItemAtVerify, 6, s_tradeGridItemAt);

static CItem*(__fastcall* s_repairGridItemAt)(CRepairGrid*, void*, int, int, int*);
static CItem* __fastcall RepairGridItemAtVerify(CRepairGrid* self, void*, int x, int y, int* topic) {
    int expectedTopic = -2;
    CItem* expected = s_repairGridItemAt(self, nullptr, x, y, &expectedTopic);
    CItem* actual = self->ItemAt(x, y, topic);
    ei::verify::Check("CRepairGrid::ItemAt", true, expected == actual && expectedTopic == *topic,
                      "(%d, %d): %p topic %d, ours %p topic %d", x, y, static_cast<void*>(expected), expectedTopic,
                      static_cast<void*>(actual), *topic);
    return actual;
}
EI_VERIFY(0x00628060, RepairGridItemAtVerify, 6, s_repairGridItemAt);

// 00628B50 / 006298B0 (the constructor pages' slot 25, ui_constructor_slots.cpp): side by side as
// above. Reached by the camp's hover over a maker page: ui_camp_deals (pages 3 and 5, the mouse on
// the first two slots).
static CItem*(__fastcall* s_spellMakerItemAt)(CConstructorPage*, void*, int, int, int*);
static CItem* __fastcall SpellMakerItemAtVerify(CConstructorPage* self, void*, int x, int y, int* topic) {
    int expectedTopic = -2;
    CItem* expected = s_spellMakerItemAt(self, nullptr, x, y, &expectedTopic);
    CItem* actual = self->SpellMakerItemAt(x, y, topic);
    ei::verify::Check("CConstructorPage::SpellMakerItemAt", true, expected == actual && expectedTopic == *topic,
                      "(%d, %d): %p topic %d, ours %p topic %d", x, y, static_cast<void*>(expected), expectedTopic,
                      static_cast<void*>(actual), *topic);
    return actual;
}
EI_VERIFY(0x00628B50, SpellMakerItemAtVerify, 6, s_spellMakerItemAt);

static CItem*(__fastcall* s_itemMakerItemAt)(CConstructorPage*, void*, int, int, int*);
static CItem* __fastcall ItemMakerItemAtVerify(CConstructorPage* self, void*, int x, int y, int* topic) {
    int expectedTopic = -2;
    CItem* expected = s_itemMakerItemAt(self, nullptr, x, y, &expectedTopic);
    CItem* actual = self->ItemMakerItemAt(x, y, topic);
    ei::verify::Check("CConstructorPage::ItemMakerItemAt", true, expected == actual && expectedTopic == *topic,
                      "(%d, %d): %p topic %d, ours %p topic %d", x, y, static_cast<void*>(expected), expectedTopic,
                      static_cast<void*>(actual), *topic);
    return actual;
}
EI_VERIFY(0x006298B0, ItemMakerItemAtVerify, 8, s_itemMakerItemAt);
