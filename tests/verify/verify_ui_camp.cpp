// Checks for the battle screen's Create, the global map, the camp pages and the hero panels
// (ui_battle_setup.cpp, ui_global_map.cpp, ui_camp.cpp, ui_hero_panels.cpp, ui_credits.cpp).
//
// These functions build windows (allocations, textures, sprites, sounds, Rebirth-patched layout
// helpers), so ours run alone and the wrappers check the structure they leave: what was added,
// the fields they set, the windows' vtables.
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/world.h"

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
                  // Create sets +0x20C to 1, then its last step runs Update (slot 24), which refreshes the
                  // selection (00630670) and clears it.
                  self->selectPending_ == 0 && !self->autosave_ && self->depth_ == 10.0f && self->centreX_ == 400.0f &&
                  self->centreY_ == 280.0f && self->weather_ && !self->dragging_;
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

// --- Equipment panel Build (00622000) ----------------------------------------------------------------

// Ours only: 8 slot regions and 12 sprites, with the body 7 more regions, 4 textures and 4 pictures;
// the rectangle 800 x 500 (body) or 100; nothing selected.
static void __fastcall EquipBuildVerify(CHeroEquipPanel* self, void*, int body) {
    Counts before = CountsOf(self);
    bool withBody = static_cast<std::uint8_t>(body) != 0;
    self->Build(withBody);
    Counts after = CountsOf(self);
    bool ok = after.regions == before.regions + (withBody ? 15 : 8) &&
              after.sceneObjects == before.sceneObjects + (withBody ? 16 : 12) &&
              after.objects >= before.objects + (withBody ? 4 : 0) && self->rect_.right == 800 &&
              self->rect_.bottom == (withBody ? 500 : 100) && self->selected_ == -1 && self->body_ == withBody;
    ei::verify::Check("CHeroEquipPanel::Build", true, ok, "body %d regions %d->%d sprites %d->%d", withBody,
                      before.regions, after.regions, before.sceneObjects, after.sceneObjects);
}
EI_VERIFY(0x00622000, EquipBuildVerify, 0, s_unused);

// --- Statistics panel Build (00622DB0) ------------------------------------------------------------

// Ours only: 2 regions, 1 surface, 12 sprites, 8 more texts; the rectangle and the two fields.
static void __fastcall StatsBuildVerify(CHeroStatsPanel* self, void*) {
    Counts before = CountsOf(self);
    int texts = *reinterpret_cast<const int*>(reinterpret_cast<std::uint8_t*>(self) + 0x164);
    self->Build();
    Counts after = CountsOf(self);
    int textsAfter = *reinterpret_cast<const int*>(reinterpret_cast<std::uint8_t*>(self) + 0x164);
    bool ok = after.regions == before.regions + 2 && after.surfaces == before.surfaces + 1 &&
              after.sceneObjects == before.sceneObjects + 12 && textsAfter == texts + 8 && self->rect_.top == 100 &&
              self->rect_.bottom == 400 && self->unknown_174_ == -1 && self->unknown_178_ == 0;
    ei::verify::Check("CHeroStatsPanel::Build", true, ok, "regions %d->%d sprites %d->%d texts %d->%d",
                      before.regions, after.regions, before.sceneObjects, after.sceneObjects, texts, textsAfter);
}
EI_VERIFY(0x00622DB0, StatsBuildVerify, 0, s_unused);

// --- Trade grid Build (006270A0) -------------------------------------------------------------------

// Ours only: 16 cell regions, 32 sprites, 4 textures, 1 surface; the lists and kinds stored.
static void __fastcall TradeGridBuildVerify(CTradeGrid* self, void*, void* bought, void* sold, int spells, int flag) {
    Counts before = CountsOf(self);
    self->Build(bought, sold, static_cast<char>(spells), static_cast<char>(flag));
    Counts after = CountsOf(self);
    bool kinds = static_cast<char>(spells) ? (self->buyKind_ == 0 && self->sellKind_ == 1)
                                           : (self->buyKind_ == 4 && self->sellKind_ == 5);
    bool ok = after.regions == before.regions + 16 && after.sceneObjects == before.sceneObjects + 32 &&
              after.objects >= before.objects + 4 && after.surfaces == before.surfaces + 1 && kinds &&
              self->bought_ == bought && self->sold_ == sold && self->rect_.left == 200 && self->rect_.bottom == 500;
    ei::verify::Check("CTradeGrid::Build", true, ok, "regions %d->%d sprites %d->%d", before.regions, after.regions,
                      before.sceneObjects, after.sceneObjects);
}
EI_VERIFY(0x006270A0, TradeGridBuildVerify, 0, s_unused);

// --- Repair grid (006279E0) and quick strip (00622A80) Builds -------------------------------------------

static void __fastcall RepairGridBuildVerify(CRepairGrid* self, void*, void* items) {
    Counts before = CountsOf(self);
    self->Build(items);
    Counts after = CountsOf(self);
    bool ok = after.regions == before.regions + 16 && after.sceneObjects == before.sceneObjects + 32 &&
              after.objects >= before.objects + 4 && after.surfaces == before.surfaces + 1 && self->items_ == items &&
              self->unknown_148 == 8;
    ei::verify::Check("CRepairGrid::Build", true, ok, "regions %d->%d sprites %d->%d", before.regions, after.regions,
                      before.sceneObjects, after.sceneObjects);
}
EI_VERIFY(0x006279E0, RepairGridBuildVerify, 0, s_unused);

static void __fastcall QuickStripBuildVerify(CHeroQuickStrip* self, void*, int top) {
    Counts before = CountsOf(self);
    bool atTop = static_cast<std::uint8_t>(top) != 0;
    self->Build(atTop);
    Counts after = CountsOf(self);
    int y = atTop ? 0 : 500;
    bool ok = after.regions == before.regions + 8 && after.sceneObjects == before.sceneObjects + 2 && self->y_ == y &&
              self->rect_.top == y && self->rect_.bottom == y + 100 && self->selected_ == -1;
    ei::verify::Check("CHeroQuickStrip::Build", true, ok, "top %d regions %d->%d", atTop, before.regions,
                      after.regions);
}
EI_VERIFY(0x00622A80, QuickStripBuildVerify, 0, s_unused);

// --- Constructor pages (006280C0 / 00628CC0) -------------------------------------------------------

static void __fastcall SpellMakerBuildVerify(CConstructorPage* self, void*, void* slots) {
    Counts before = CountsOf(self);
    int texts = *reinterpret_cast<const int*>(reinterpret_cast<std::uint8_t*>(self) + 0x160);
    self->BuildSpellMaker(slots);
    Counts after = CountsOf(self);
    int textsAfter = *reinterpret_cast<const int*>(reinterpret_cast<std::uint8_t*>(self) + 0x160);
    bool ok = after.regions == before.regions + 12 && after.sceneObjects == before.sceneObjects + 5 &&
              after.surfaces == before.surfaces + 1 && after.objects >= before.objects + 4 && textsAfter == texts + 2 &&
              self->kind_ == 6 && self->slots_ == slots && self->selected_ == 0;
    ei::verify::Check("CConstructorPage::BuildSpellMaker", true, ok, "regions %d->%d sprites %d->%d", before.regions,
                      after.regions, before.sceneObjects, after.sceneObjects);
}
EI_VERIFY(0x006280C0, SpellMakerBuildVerify, 0, s_unused);

static void __fastcall ItemMakerBuildVerify(CConstructorPage* self, void*, void* slots) {
    Counts before = CountsOf(self);
    self->BuildItemMaker(slots);
    Counts after = CountsOf(self);
    bool ok = after.regions == before.regions + 11 && after.sceneObjects == before.sceneObjects + 5 &&
              after.objects >= before.objects + 4 && self->kind_ == 7 && self->slots_ == slots && self->selected_ == 0;
    ei::verify::Check("CConstructorPage::BuildItemMaker", true, ok, "regions %d->%d sprites %d->%d", before.regions,
                      after.regions, before.sceneObjects, after.sceneObjects);
}
EI_VERIFY(0x00628CC0, ItemMakerBuildVerify, 0, s_unused);

// --- Global map Update (0062F050) --------------------------------------------------------------------

// Every frame; it draws the weather (whose animation advances with each call), so ours run alone:
// the bob time advanced by the frame time (stored as float), the turn's w not negative after a
// turn, the zone selection refresh done, the arrow sprite turned like the model.
namespace {
__declspec(naked) double __fastcall VerifyFrameTime(CUiWindow*) {
    __asm {
        mov eax, 0x00600270
        jmp eax
    }
}
} // namespace
static void __fastcall GlobalMapUpdateVerify(CGlobalMap* self, void*) {
    float before = self->bobTime_;
    volatile float expected = static_cast<float>(VerifyFrameTime(self) + static_cast<double>(before));
    bool turning = self->turning_ != 0 || self->dragging_;
    self->Update();
    float actual = self->bobTime_;
    bool ok = std::memcmp(&actual, const_cast<float*>(&expected), 4) == 0 && self->selectPending_ == 0 &&
              (!turning || !(self->turn_[0] < 0.0f));
    ei::verify::Check("CGlobalMap::Update", true, ok, "bob %g -> %g (expected %g) turning %d w %g", before, actual,
                      static_cast<float>(expected), turning, self->turn_[0]);
}
EI_VERIFY(0x0062F050, GlobalMapUpdateVerify, 0, s_unused);

// --- Camp prices (0060FAB0, slot 54) -------------------------------------------------------------

// PUSH ECX; PUSH EBX; MOV EBX,[ESP+0x10] (6 bytes). Pure (the item's price slot reads only).
// Rebirth's FixRepairPrice patches +0x2C: with the flag on in a Rebirth launch this compares with
// the patched original.
static int(__fastcall* s_campPrice)(CCampPages*, void*, std::uint8_t*, int);
static int __fastcall CampPriceVerify(CCampPages* self, void*, std::uint8_t* item, int kind) {
    int expected = s_campPrice(self, nullptr, item, kind);
    float durability, maximum;
    std::memcpy(&durability, item + 0x20, 4);
    std::memcpy(&maximum, item + 0x24, 4);
    ei::verify::Check("CCampPages::Price", expected, self->Price(item, kind), "kind %d type %X wear %g/%g",
                      kind, *reinterpret_cast<const int*>(item + 0x10), durability, maximum);
    return expected;
}
EI_VERIFY(0x0060FAB0, CampPriceVerify, 6, s_campPrice);

// 0060E6B0: ours only (it frees and releases). Afterwards the heroes array and the constructed
// item and spell are gone, and the local player's records have their +0x1C back.
static void __fastcall CampCloseVerify(CCampPages* self, void*) {
    std::uint8_t* player = LocalPlayerData();
    auto sum = [player] {
        std::uint32_t total = 0;
        std::uint8_t* records = *reinterpret_cast<std::uint8_t**>(player + 0x30);
        int count = *reinterpret_cast<int*>(player + 0x34);
        for (int i = 0; i < count; ++i) total += *reinterpret_cast<std::uint32_t*>(records + i * 0x784 + 0x1C);
        return total;
    };
    std::uint32_t before = sum();
    self->Close();
    std::uint32_t after = sum();
    bool ok = !self->heroes_ && !self->itemMade_ && !self->spellMade_ && before == after;
    ei::verify::Check("CCampPages::Close", true, ok, "heroes %p item %p spell %p, records %08X -> %08X", self->heroes_,
                      self->itemMade_, self->spellMade_, before, after);
}
EI_VERIFY(0x0060E6B0, CampCloseVerify, 0, s_unused);

// 0060E3E0: ours only (a destructor body). The window base must be left empty.
static void __fastcall TradePanelDestroyVerify(CUiWindow* self) {
    TradePanelDestroy(self);
    ei::verify::Check("TradePanelDestroy", true, !self->sceneObjects_.data_ && !self->objects_.data_,
                      "scene %p objects %p", self->sceneObjects_.data_, self->objects_.data_);
}
EI_VERIFY(0x0060E3E0, TradePanelDestroyVerify, 0, s_unused);

// 0060FE40: ours only (it moves the item). Other kinds return 0; a weapon must be gone from the
// hero's weapons, with the chosen index below their count (or the count 0 and the index -1).
static int __fastcall CampTakeOffVerify(CCampPages* self, void*, std::uint8_t* item) {
    int kind;
    std::memcpy(&kind, item + 0x10, 4);
    using HeroFn = std::uint8_t*(__thiscall*)(CCampPages*);
    std::uint8_t* hero = (kind == 0x3004) ? reinterpret_cast<HeroFn>(self->vtable_[51])(self) : nullptr;
    int result = self->TakeOff(item);
    bool ok = true;
    if (kind < 0x3004 || (kind > 0x3006 && kind != 0x3008)) ok = result == 0;
    if (hero) {
        int count, chosen;
        std::uint8_t** weapons;
        std::memcpy(&count, hero + 0x734, 4);
        std::memcpy(&weapons, hero + 0x730, 4);
        std::memcpy(&chosen, hero + 0x774, 4);
        for (int i = 0; i < count; ++i)
            if (weapons[i] == item) ok = false;
        if (!(static_cast<unsigned>(chosen) < static_cast<unsigned>(count) || (count == 0 && chosen == -1))) ok = false;
    }
    ei::verify::Check("CCampPages::TakeOff", true, ok, "kind %X result %d", kind, result);
    return result;
}
EI_VERIFY(0x0060FE40, CampTakeOffVerify, 0, s_unused);

// 0060FCA0: ours only (it moves the item). A weapon put on a hero with room must be among the
// hero's weapons afterwards.
static void __fastcall CampPutOnVerify(CCampPages* self, void*, std::uint8_t* item) {
    int kind;
    std::memcpy(&kind, item + 0x10, 4);
    using HeroFn = std::uint8_t*(__thiscall*)(CCampPages*);
    std::uint8_t* hero = reinterpret_cast<HeroFn>(self->vtable_[51])(self);
    unsigned before;
    std::memcpy(&before, hero + 0x734, 4);
    self->PutOn(item);
    if (kind != 0x3004 || before >= 4) return;
    int count;
    std::uint8_t** weapons;
    std::memcpy(&count, hero + 0x734, 4);
    std::memcpy(&weapons, hero + 0x730, 4);
    bool found = false;
    for (int i = 0; i < count; ++i)
        if (weapons[i] == item) found = true;
    ei::verify::Check("CCampPages::PutOn", true, found, "weapons %u -> %d", before, count);
}
EI_VERIFY(0x0060FCA0, CampPutOnVerify, 0, s_unused);

// 00610920 / 00610AD0: ours only (they move items between lists). On the equipment and level-up
// pages (0 / 1), where CanEquip only reads, the hero's equipment is checked: a weapon put on a
// hero with room is among its weapons, an item taken off (mode 0) is neither among its weapons
// nor its armour, a refused move leaves both counts alone. Other pages: the page stays.
namespace {
struct HeroGear {
    int weapons, armour;
    bool holds;
};
HeroGear GearOf(CCampPages* self, const std::uint8_t* item) {
    using HeroFn = std::uint8_t*(__thiscall*)(CCampPages*);
    std::uint8_t* hero = reinterpret_cast<HeroFn>(self->vtable_[51])(self);
    HeroGear gear{};
    std::uint8_t** list;
    std::memcpy(&gear.weapons, hero + 0x734, 4);
    std::memcpy(&list, hero + 0x730, 4);
    for (int i = 0; i < gear.weapons; ++i)
        if (list[i] == item) gear.holds = true;
    std::memcpy(&gear.armour, hero + 0x754, 4);
    std::memcpy(&list, hero + 0x750, 4);
    for (int i = 0; i < gear.armour; ++i)
        if (list[i] == item) gear.holds = true;
    return gear;
}
int KindOf(const std::uint8_t* item) {
    int kind;
    std::memcpy(&kind, item + 0x10, 4);
    return kind;
}
} // namespace

static void __fastcall CampPutOnFromStripVerify(CCampPages* self, void*, std::uint8_t* item) {
    int page = self->page_;
    bool equipPage = page == 0 || page == 1;
    HeroGear before = equipPage ? GearOf(self, item) : HeroGear{};
    int kind = KindOf(item);
    self->PutOnFromStrip(item);
    bool ok = self->page_ == page;
    if (equipPage && kind == 0x3004 && before.weapons < 4) ok = ok && GearOf(self, item).holds;
    ei::verify::Check("CCampPages::PutOnFromStrip", true, ok, "page %d kind %X weapons %d", page, kind, before.weapons);
}
EI_VERIFY(0x00610920, CampPutOnFromStripVerify, 0, s_unused);

static void __fastcall CampMoveItemVerify(CCampPages* self, void*, std::uint8_t* item, int mode) {
    int page = self->page_;
    bool equipPage = page == 0 || page == 1;
    bool accepted = false;
    HeroGear before{};
    if (equipPage) {
        accepted = self->CanEquip(item, mode);
        before = GearOf(self, item);
    }
    int kind = KindOf(item);
    self->MoveItem(item, mode);
    bool ok = self->page_ == page;
    if (equipPage) {
        HeroGear after = GearOf(self, item);
        if (!accepted)
            ok = ok && after.weapons == before.weapons && after.armour == before.armour;
        else if (mode == 0 && (kind == 0x3004 || kind == 0x3005))
            ok = ok && !after.holds;
        else if (mode == 1 && kind == 0x3004 && before.weapons < 4)
            ok = ok && after.holds;
    }
    ei::verify::Check("CCampPages::MoveItem", true, ok, "page %d mode %d kind %X accepted %d", page, mode, kind,
                      accepted);
}
EI_VERIFY(0x00610AD0, CampMoveItemVerify, 0, s_unused);

// 0060F9E0 / 0060F980: ours only (they reach the hero's unit and figure). The chosen weapon index
// must stay as it was (the functions only read it).
static void __fastcall CampRefreshWeaponsVerify(CCampPages* self, void*) {
    using HeroFn = std::uint8_t*(__thiscall*)(CCampPages*);
    std::uint8_t* hero = reinterpret_cast<HeroFn>(self->vtable_[51])(self);
    int chosen;
    std::memcpy(&chosen, hero + 0x774, 4);
    self->RefreshWeapons();
    int after;
    std::memcpy(&after, hero + 0x774, 4);
    ei::verify::Check("CCampPages::RefreshWeapons", true, chosen == after, "chosen %d -> %d", chosen, after);
}
EI_VERIFY(0x0060F9E0, CampRefreshWeaponsVerify, 0, s_unused);
static void __fastcall CampRefreshStatsVerify(CCampPages* self, void*) {
    int hero = self->hero_;
    self->RefreshStats();
    ei::verify::Check("CCampPages::RefreshStats", true, self->hero_ == hero, "hero %d -> %d", hero, self->hero_);
}
EI_VERIFY(0x0060F980, CampRefreshStatsVerify, 0, s_unused);

// 0060F6B0: ours only (time-dependent). Without a turn button held the turn stays.
static void __fastcall CampUpdateVerify(CCampPages* self, void*) {
    int turning;
    std::memcpy(&turning, reinterpret_cast<std::uint8_t*>(self) + 0x25C, 4);
    std::uint32_t before = self->heroTurn_;
    self->Update();
    if (turning == 1 || turning == 2) return;
    ei::verify::Check("CCampPages::Update", true, self->heroTurn_ == before, "turn %08X -> %08X", before,
                      self->heroTurn_);
}
EI_VERIFY(0x0060F6B0, CampUpdateVerify, 0, s_unused);

// 0060F710 / 0060F7A0: ours only. The hero chosen is a valid index.
static void __fastcall CampNextHeroVerify(CCampPages* self, void*) {
    self->NextHero();
    int count;
    std::memcpy(&count, LocalPlayerData() + 0x34, 4);
    ei::verify::Check("CCampPages::NextHero", true, self->hero_ >= 0 && self->hero_ < count, "hero %d of %d",
                      self->hero_, count);
}
EI_VERIFY(0x0060F710, CampNextHeroVerify, 0, s_unused);
static void __fastcall CampPreviousHeroVerify(CCampPages* self, void*) {
    self->PreviousHero();
    int count;
    std::memcpy(&count, LocalPlayerData() + 0x34, 4);
    ei::verify::Check("CCampPages::PreviousHero", true, self->hero_ >= 0 && self->hero_ < count, "hero %d of %d",
                      self->hero_, count);
}
EI_VERIFY(0x0060F7A0, CampPreviousHeroVerify, 0, s_unused);

// 0060F830: ours only (it sets the panels' text).
static void __fastcall CampShowHeroVerify(CCampPages* self, void*) {
    int hero = self->hero_;
    self->ShowHero();
    ei::verify::Check("CCampPages::ShowHero", true, self->hero_ == hero, "hero %d -> %d", hero, self->hero_);
}
EI_VERIFY(0x0060F830, CampShowHeroVerify, 0, s_unused);

// 005F67A0: ours only (a constructor). Vtables, empty arrays, the identity turn.
static CGlobalMap* __fastcall GlobalMapConstructVerify(CGlobalMap* self, void*, CInterface3D* ui) {
    CGlobalMap* result = self->Construct(ui);
    bool ok = result == self && self->vtable_ == reinterpret_cast<void**>(0x00740B3C) && self->interface_ == ui &&
              self->zones_.size == 0 && self->links_.size == 0 && self->quests_.size == 0 && self->turn_[0] == 1.0f;
    ei::verify::Check("CGlobalMap::Construct", true, ok, "vtable %p zones %d links %d quests %d", self->vtable_,
                      self->zones_.size, self->links_.size, self->quests_.size);
    return result;
}
EI_VERIFY(0x005F67A0, GlobalMapConstructVerify, 0, s_unused);

// 005F6920: ours only (a destructor body). The arrays' vtables put back, the window base emptied.
static void __fastcall GlobalMapDestroyVerify(CGlobalMap* self) {
    GlobalMapDestroy(self);
    bool ok = self->zones_.vtable == reinterpret_cast<void*>(0x00740BFC) &&
              self->links_.vtable == reinterpret_cast<void*>(0x00740BF0) &&
              self->quests_.vtable == reinterpret_cast<void*>(0x00740BE4) && !self->sceneObjects_.data_;
    ei::verify::Check("GlobalMapDestroy", true, ok, "scene %p", self->sceneObjects_.data_);
}
EI_VERIFY(0x005F6920, GlobalMapDestroyVerify, 0, s_unused);

// 005F6240: ours only (a constructor). Vtables, the flag, empty caches with limit 50.
static CCampPages* __fastcall CampConstructVerify(CCampPages* self, void*, CInterface3D* ui, int fromHeroScreen) {
    CCampPages* result = self->Construct(ui, fromHeroScreen);
    std::uint32_t modelsCount, modelsLimit, texturesCount, texturesLimit;
    auto* bytes = reinterpret_cast<std::uint8_t*>(self);
    std::memcpy(&modelsCount, bytes + 0x174, 4);
    std::memcpy(&modelsLimit, bytes + 0x17C, 4);
    std::memcpy(&texturesCount, bytes + 0x184, 4);
    std::memcpy(&texturesLimit, bytes + 0x18C, 4);
    bool ok = result == self && self->vtable_ == reinterpret_cast<void**>(0x0074091C) && self->interface_ == ui &&
              self->fromHeroScreen_ == (static_cast<std::uint8_t>(fromHeroScreen) != 0) && modelsCount == 0 &&
              texturesCount == 0 && modelsLimit == 50 && texturesLimit == 50;
    ei::verify::Check("CCampPages::Construct", true, ok, "vtable %p caches %u/%u %u/%u", self->vtable_, modelsCount,
                      modelsLimit, texturesCount, texturesLimit);
    return result;
}
EI_VERIFY(0x005F6240, CampConstructVerify, 0, s_unused);
