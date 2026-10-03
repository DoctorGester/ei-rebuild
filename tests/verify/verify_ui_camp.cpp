// Checks for the battle screen's Create, the global map, the camp pages and the hero panels
// (ui_battle_setup.cpp, ui_global_map.cpp, ui_camp.cpp, ui_hero_panels.cpp, ui_credits.cpp).
//
// These functions build windows (allocations, textures, sprites, sounds, Rebirth-patched layout
// helpers), so ours run alone and the wrappers check the structure they leave: what was added,
// the fields they set, the windows' vtables.
#include "verify.h"

#include "../../src/game/ui_camp.h"

#include <cstring>
#include <initializer_list>

namespace {
void* s_unused;

struct Counts {
    int objects, sceneObjects, surfaces, regions, models;
};
Counts CountsOf(CUiWindow* w) {
    return Counts{w->objects_.size_, w->sceneObjects_.size_, w->surfaces_.size_, w->regions_.size_,
                  w->models_.size_};
}
std::uintptr_t VtableOf(const CUiWindow* w) { return w ? reinterpret_cast<std::uintptr_t>(w->vtable_) : 0; }
int InputCount(CUiContainer* w) { return static_cast<int>(w->inputsEnd_ - w->inputsBegin_); }
} // namespace

// --- Battle screen (00635250) --------------------------------------------------------------------

// Eleven panels appended as children in their order (each with its vtable), two input handlers,
// three textures, the fields Create resets, one more hero id in a new game.
static void __fastcall BattleScreenCreateVerify(CBattleScreenSetup* self, void*) {
    Counts before = CountsOf(self);
    int children = self->children_.size_;
    int inputs = InputCount(self);
    int heroes = self->heroes_.size_;
    bool loading = *reinterpret_cast<const int*>(0x007AFE1C) != 0;
    self->Create();
    Counts after = CountsOf(self);
    CUiWindow* panels[11] = {self->weaponBar_, self->quickItems_, self->spellColumn_, self->actionColumn_,
                             self->portraits_, self->unitPanel_, self->map_, self->clock_,
                             self->movePanel_, self->messageLog_, self->topPanel_};
    bool order = self->children_.size_ == children + 11;
    for (int i = 0; order && i < 11; ++i) order = self->children_.data_[children + i] == panels[i];
    bool vtables = VtableOf(self->unitPanel_) == 0x007426F8 && VtableOf(self->map_) == 0x00742660 &&
                   VtableOf(self->clock_) == 0x007425C8 && VtableOf(self->movePanel_) == 0x00742530 &&
                   VtableOf(self->topPanel_) == 0x00742498 &&
                   reinterpret_cast<std::uintptr_t>(self->unitPanel_->inputVtable_) == 0x007426C4;
    bool owners = true;
    for (CUiWindow* p : {self->unitPanel_, self->map_, self->clock_, self->movePanel_, self->topPanel_})
        owners = owners && *reinterpret_cast<CUiWindow**>(reinterpret_cast<std::uint8_t*>(p) + 0x148) == self;
    bool fields = self->unknown_210 == -1 && self->unknown_17C == 0 && self->unknown_180 == -1 &&
                  self->portraitMode_ == 0 && self->unknown_164 == 0 && self->unknown_1FC == 0 &&
                  !self->unknown_1C0 && !self->unknown_1C1 &&
                  (*reinterpret_cast<const std::uint32_t*>(0x0079B2B0) & 0x40) == 0;
    bool heroOk = loading || self->heroes_.size_ == heroes + (self->hasHero_ ? 1 : 0);
    bool ok = order && vtables && owners && fields && heroOk && InputCount(self) == inputs + 2 &&
              after.objects >= before.objects + 3;
    ei::verify::Check("CBattleScreenSetup::Create", true, ok,
                      "order %d vtables %d owners %d fields %d heroes %d->%d inputs %d->%d objects %d->%d", order,
                      vtables, owners, fields, heroes, self->heroes_.size_, inputs, InputCount(self),
                      before.objects, after.objects);
}
EI_VERIFY(0x00635250, BattleScreenCreateVerify, 0, s_unused);

// --- Global map (0062D350) -----------------------------------------------------------------------

// The arrays grow by exactly the counts Create keeps, the counts add up, a link points back at its
// zone and a zone that names a link is in state 2; 13 fixed sprites plus one per marker, 3 fixed
// regions plus one per link, two listed surfaces and the backdrop; the fixed fields.
static void __fastcall GlobalMapCreateVerify(CGlobalMap* self, void*) {
    Counts before = CountsOf(self);
    int zones = self->zones_.size, links = self->links_.size, quests = self->quests_.size;
    self->Create();
    Counts after = CountsOf(self);
    int markers = self->zoneCount_ + self->questCount_ + self->linkCount_;
    bool counts = self->markerCount_ == markers && self->zones_.size == zones + self->zoneCount_ &&
                  self->links_.size == links + self->linkCount_ && self->quests_.size == quests + self->questCount_;
    bool layout = after.sceneObjects == before.sceneObjects + 13 + markers &&
                  after.regions == before.regions + 3 + self->linkCount_ &&
                  after.surfaces == before.surfaces + 2 && after.objects >= before.objects + 5 && self->backdrop_;
    bool links_ok = true;
    for (int i = 0; i < self->linkCount_ && links_ok; ++i)
        links_ok = self->links_.data[links + i].zone >= 0 && self->links_.data[links + i].zone < self->zoneCount_;
    for (int i = 0; i < self->zoneCount_ && links_ok; ++i) {
        const GlobalMapZone& z = self->zones_.data[zones + i];
        links_ok = z.link == -1 || (z.state == 2 && z.link >= 0 && z.link < self->linkCount_);
    }
    static const int kWeather[18] = {0, 0, 10, 0xFF, 0x9B, 0, 0xE1, 0x64, 0x9B, 0, 0x7D, 0x6E, 0x69, 10, 0xC3, 0xB9,
                                     0x77, 0xF8};
    bool fields = std::memcmp(self->weatherTable_, kWeather, sizeof(kWeather)) == 0 && self->weatherIndex_ == 17 &&
                  self->unknown_20C == 1 && !self->autosave_ && self->depth_ == 10.0f && self->centreX_ == 400.0f &&
                  self->centreY_ == 280.0f && self->weather_ && !self->unknown_264;
    bool ok = counts && layout && links_ok && fields;
    ei::verify::Check("CGlobalMap::Create", true, ok,
                      "counts %d layout %d links %d fields %d zones %d quests %d links %d sprites %d->%d regions %d->%d",
                      counts, layout, links_ok, fields, self->zoneCount_, self->questCount_, self->linkCount_,
                      before.sceneObjects, after.sceneObjects, before.regions, after.regions);
}
EI_VERIFY(0x0062D350, GlobalMapCreateVerify, 0, s_unused);

// --- Camp page switch (0060E9E0) -------------------------------------------------------------------

// The page shown afterwards (the requested one, or the previous one when the camp does not offer
// it) has exactly its panels enabled, the item strip its slot count, its button full bright and
// the others half; page 7 leaves (the pending value when opened from the hero screen).
static void __fastcall CampShowPageVerify(CCampPages* self, void*, int page) {
    int previous = self->page_;
    bool pendingBefore = self->interface_->pendingFlag_;
    self->ShowPage(page);
    if (page == 7) {
        bool ok = !self->fromHeroScreen_ || (self->interface_->pendingFlag_ && self->interface_->pendingValue_ == 0);
        ei::verify::Check("CCampPages::ShowPage", true, ok, "leave: pending %d->%d", pendingBefore,
                          self->interface_->pendingFlag_);
        return;
    }
    int shown = self->page_;
    bool offered = page < 0 || page > 7 || (page < 2) || (page <= 3 && self->spellConstructor_ && (page == 2 || !self->noConstructor_)) ||
                   (page >= 4 && self->itemConstructor_ && (page != 5 || !self->noConstructor_));
    bool pageOk = offered ? shown == page : shown == previous;
    auto on = [](CUiWindow* w) { return w->enabled_ != 0; };
    auto strip = [](CUiItemStrip* s) { return reinterpret_cast<CUiWindow*>(s)->enabled_ != 0; };
    bool panels = on(self->statsPanel_) == (shown == 0 || shown == 1) && on(self->equipment_) == (shown == 0) &&
                  on(self->equipment2_) == (shown == 1) && on(self->trade_) == (shown == 1) &&
                  on(self->helpPanel_) == (shown >= 2 && shown <= 6);
    if (self->spellConstructor_)
        panels = panels && on(self->spellConstructor1_) == (shown == 2) && on(self->spellConstructor2_) == (shown == 3) &&
                 strip(self->spellStrip_) == (shown == 2 || shown == 3);
    if (self->itemConstructor_)
        panels = panels && on(self->itemConstructor1_) == (shown == 4) && on(self->itemConstructor2_) == (shown == 5) &&
                 on(self->itemConstructor3_) == (shown == 6) && strip(self->partStrip_) == (shown >= 4 && shown <= 6);
    int slots = shown == 2 ? 1 : shown == 4 ? 5 : 9;
    bool shades = true;
    for (int i = 2; i < 10 && shown >= 0 && shown < 8; ++i) {
        std::uint32_t bits;
        std::memcpy(&bits, reinterpret_cast<std::uint8_t*>(self->sceneObjects_.data_[i]) + 0x224, 4);
        shades = shades && bits == (i == shown + 2 ? 0x3F800000u : 0x3F000000u);
    }
    bool ok = pageOk && panels && self->itemStrip_->slots_ == slots && shades;
    ei::verify::Check("CCampPages::ShowPage", true, ok, "page %d previous %d shown %d panels %d slots %d shades %d",
                      page, previous, shown, panels, self->itemStrip_->slots_, shades);
}
EI_VERIFY(0x0060E9E0, CampShowPageVerify, 0, s_unused);

// --- Camp CanEquip (006100E0) ------------------------------------------------------------------------

// 006100E0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x14; PUSH EBX; PUSH ESI (8 bytes). A predicate: the
// original and ours side by side, AL compared (the list refresh of page 5 runs in both).
static bool(__fastcall* s_campCanEquip)(CCampPages*, void*, std::uint8_t*, int);
static bool __fastcall CampCanEquipVerify(CCampPages* self, void*, std::uint8_t* item, int mode) {
    bool expected = s_campCanEquip(self, nullptr, item, mode);
    bool actual = self->CanEquip(item, mode);
    ei::verify::Check("CCampPages::CanEquip", expected, actual, "page %d mode %d type %x", self->page_, mode,
                      *reinterpret_cast<const int*>(item + 0x10));
    return expected;
}
EI_VERIFY(0x006100E0, CampCanEquipVerify, 8, s_campCanEquip);

// --- Camp UpdateAction (00611930) --------------------------------------------------------------------

namespace {
struct ActionState {
    int cost;
    bool enabled, shown, deconstructing, itemMade, spellMade;
    std::uint32_t shade0, shade1;
};
ActionState ActionOf(CCampPages* c) {
    ActionState s{c->cost_, c->actionEnabled_, c->actionShown_, c->deconstructing_, c->itemMade_ != nullptr,
                  c->spellMade_ != nullptr, 0, 0};
    std::memcpy(&s.shade0, reinterpret_cast<std::uint8_t*>(c->sceneObjects_.data_[0]) + 0x224, 4);
    std::memcpy(&s.shade1, reinterpret_cast<std::uint8_t*>(c->sceneObjects_.data_[1]) + 0x224, 4);
    return s;
}
} // namespace

// 00611930: PUSH -1; PUSH 0x72B8D0 (7 bytes). Side by side: the original first, then ours (which
// releases what the original's constructor check made and makes it again); the cost, the flags,
// whether something would be made and the buttons' shades compared.
static void(__fastcall* s_campUpdateAction)(CCampPages*, void*);
static void __fastcall CampUpdateActionVerify(CCampPages* self, void*) {
    s_campUpdateAction(self, nullptr);
    ActionState expected = ActionOf(self);
    self->UpdateAction();
    ActionState actual = ActionOf(self);
    bool same = expected.cost == actual.cost && expected.enabled == actual.enabled && expected.shown == actual.shown &&
                expected.deconstructing == actual.deconstructing && expected.itemMade == actual.itemMade &&
                expected.spellMade == actual.spellMade && expected.shade0 == actual.shade0 &&
                expected.shade1 == actual.shade1;
    ei::verify::Check("CCampPages::UpdateAction", true, same, "page %d cost %d/%d enabled %d/%d shown %d/%d",
                      self->page_, expected.cost, actual.cost, expected.enabled, actual.enabled, expected.shown,
                      actual.shown);
}
EI_VERIFY(0x00611930, CampUpdateActionVerify, 7, s_campUpdateAction);

// --- Camp Draw (0060F050) ----------------------------------------------------------------------------

// Every frame; ours only: returns 1, the page and the hero unchanged, no item found over a page
// button whose help is shown (the search stops at a help topic).
static BOOL __fastcall CampDrawVerify(CCampPages* self, void*, int pass) {
    int page = self->page_, hero = self->hero_;
    int region = self->RegionAt(*reinterpret_cast<const int*>(0x007C7C50), *reinterpret_cast<const int*>(0x007C7C54));
    BOOL result = self->Draw(pass);
    bool alwaysTopic = region == 4 || region == 5 || region == 11;
    bool ok = result == 1 && self->page_ == page && self->hero_ == hero && !(alwaysTopic && self->hoveredItem_);
    ei::verify::Check("CCampPages::Draw", true, ok, "page %d region %d item %p", page, region, self->hoveredItem_);
    return result;
}
EI_VERIFY(0x0060F050, CampDrawVerify, 0, s_unused);

// --- Constructors' checks (00612290 / 00612600) -------------------------------------------------------

// They make objects (the spell, the item), so ours run alone: the result implies something was
// made (with the reference taken), nothing is made without a spell (item) slot.
static bool __fastcall CampCanMakeSpellVerify(CCampPages* self, void*) {
    std::uint8_t* before = self->spellMade_;
    bool result = self->CanMakeSpell();
    bool ok = !result || (self->spellMade_ && *reinterpret_cast<int*>(self->spellMade_ + 4) >= 1);
    ei::verify::Check("CCampPages::CanMakeSpell", true, ok, "result %d made %p (before %p)", result, self->spellMade_,
                      before);
    return result;
}
EI_VERIFY(0x00612290, CampCanMakeSpellVerify, 0, s_unused);

static bool __fastcall CampCanMakeItemVerify(CCampPages* self, void*) {
    std::uint8_t* before = self->itemMade_;
    bool result = self->CanMakeItem();
    bool ok = result == (self->itemMade_ != before && self->itemMade_ != nullptr) &&
              (!result || *reinterpret_cast<int*>(self->itemMade_ + 4) >= 1);
    ei::verify::Check("CCampPages::CanMakeItem", true, ok, "result %d made %p (before %p)", result, self->itemMade_,
                      before);
    return result;
}
EI_VERIFY(0x00612600, CampCanMakeItemVerify, 0, s_unused);

// --- Hero statistics panel (006233C0 / 00623880) -----------------------------------------------------

// Ours only: the two fixed regions kept plus the page's rows (9 equipment, 13 new hero, 8 other),
// the page stored, the scene objects from 4 on shown exactly on the new hero page.
static void __fastcall StatsShowVerify(CHeroStatsPanel* self, void*, int page) {
    self->ShowStats(page);
    int rows = page == 0 ? 9 : page == 10 ? 13 : 8;
    bool shown = true;
    for (int i = 4; i < self->sceneObjects_.size_; ++i)
        shown = shown && *reinterpret_cast<const int*>(reinterpret_cast<std::uint8_t*>(self->sceneObjects_.data_[i]) +
                                                       0x220) == (page == 10 ? 1 : 0);
    bool ok = self->page_ == page && self->regions_.size_ == 2 + rows && shown;
    ei::verify::Check("CHeroStatsPanel::ShowStats", true, ok, "page %d regions %d", page, self->regions_.size_);
}
EI_VERIFY(0x006233C0, StatsShowVerify, 0, s_unused);

// Drawing only (text through GDI); ours only: the panel's state is left alone.
static void __fastcall StatsDrawVerify(CHeroStatsPanel* self, void*) {
    int page = self->page_, regions = self->regions_.size_, points = self->points_;
    self->DrawStats();
    bool ok = self->page_ == page && self->regions_.size_ == regions && self->points_ == points;
    ei::verify::Check("CHeroStatsPanel::DrawStats", true, ok, "page %d", page);
}
EI_VERIFY(0x00623880, StatsDrawVerify, 0, s_unused);
