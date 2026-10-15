// Check for the camp's Create (0060C870, src/game/ui_camp_create.cpp; A-I22). Rebirth's WideScreen
// replaces the function (the original, then two curtains over the screen's margins), which the
// rewrite reproduces, so it runs in both launches.
//
// Ours only: Create allocates the camp's panels, lists and the client world's hero units, so a
// second run cannot be compared. What it leaves is read back: twelve regions, ten button sprites,
// two surfaces, the panels every camp has (with WideScreen two more sprites, the curtains: shown,
// tint 0.3), eight page titles, the first page by what the camp
// offers, a unit for every hero record and the records' ids put back.
// Scenario: ui_camp opens the camp (and ui_camp_deals, ui_trade_hold, ui_describe; ui_widescreen
// with WideScreen).
#include "verify.h"

#include "../../src/game/class_templates.h"
#include "../../src/game/party.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_widescreen.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"

#include <cstdint>

namespace {
void* s_unused;
constexpr int kIdsKept = 16;
constexpr int kButtonSprites = 10;

// WideScreen's curtains: sprites `first` and `first` + 1, shown or not, tint 0.3.
bool CurtainsAre(const CUiWindow* window, int first, int visible) {
    for (int i = first; i < first + 2; ++i) {
        const CUiMesh* curtain = window->SceneMesh(i);
        if (curtain->visible_ != visible || curtain->tint_[0] != 0.3f || curtain->tint_[1] != 0.3f ||
            curtain->tint_[2] != 0.3f)
            return false;
    }
    return true;
}
} // namespace

static void __fastcall CampCreateVerify(CCampPages* self) {
    // The records' ids before (the first few are enough).
    std::uint32_t ids[kIdsKept] = {};
    {
        auto* before = reinterpret_cast<CParty*>(LocalPlayerData());
        for (int i = 0; i < before->members_.size && i < kIdsKept; ++i) ids[i] = before->members_.data[i].unitId;
    }
    self->Create();
    // No class template for the player's "constr.current": back to the world, nothing built.
    if (!self->constructor_ && self->regions_.size == 0) return;

    const bool wide = widescreen::Active();
    const int sprites = kButtonSprites + (wide ? 2 : 0);
    bool built = self->regions_.size == 12 && self->sceneObjects_.size == sprites &&
                 (!wide || CurtainsAre(self, kButtonSprites, 1)) && self->surfaces_.size == 2 &&
                 self->descPanel_ && self->statsPanel_ && self->helpPanel_ && self->itemStrip_ && self->equipment_ &&
                 self->equipment2_ && self->trade_ && self->pageTitles_.size == 8 && self->turning_ == 0 &&
                 !self->itemMade_ && !self->spellMade_;
    ei::verify::Check("CCampPages::Create built", true, built, "regions %d sprites %d surfaces %d titles %d",
                      self->regions_.size, self->sceneObjects_.size, self->surfaces_.size, self->pageTitles_.size);

    bool offered = (!self->spellConstructor_ || (self->spellStrip_ && self->spellConstructor1_ && self->spellConstructor2_)) &&
                   (!self->itemConstructor_ || (self->partStrip_ && self->itemConstructor1_ && self->itemConstructor2_ &&
                                                self->itemConstructor3_));
    int page = self->itemConstructor_ ? 4 : self->spellConstructor_ ? 2 : 0;
    ei::verify::Check("CCampPages::Create pages", true, offered && self->page_ == page, "spell %d item %d page %d",
                      self->spellConstructor_, self->itemConstructor_, self->page_);

    auto* player = reinterpret_cast<CParty*>(LocalPlayerData());
    bool heroes = self->heroes_.size == player->members_.size;
    for (int i = 0; heroes && i < player->members_.size && i < kIdsKept; ++i)
        heroes = player->members_.data[i].unitId == ids[i];
    ei::verify::Check("CCampPages::Create heroes", true,
                      heroes && self->hero_ >= 0 && self->hero_ < self->heroes_.size && self->heroes_[self->hero_] != nullptr,
                      "heroes %d party %d shown %d", self->heroes_.size, player->members_.size, self->hero_);

    if (self->constructor_)
        ei::verify::Check("CCampPages::Create stock", 0, static_cast<int>(self->constructor_->flag38), "");
}
EI_VERIFY(0x0060C870, CampCreateVerify, 0, s_unused);
