// Check for the vanilla body of the camp's Create (0060C870, src/game/ui_camp_create.cpp; A-I22).
// Rebirth patches the function, so the installer hooks it in the vanilla launch only.
//
// Ours only: Create allocates the camp's panels, lists and the client world's hero units, so a
// second run cannot be compared. What it leaves is read back: twelve regions, ten button sprites,
// two surfaces, the panels every camp has, eight page titles, the first page by what the camp
// offers, a unit for every hero record and the records' ids put back.
// Scenario: ui_camp opens the camp (and ui_camp_deals, ui_trade_hold, ui_describe), in the vanilla
// launch.
#include "verify.h"

#include "../../src/game/class_templates.h"
#include "../../src/game/party.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/unit.h"
#include "../../src/game/world.h"

#include <cstdint>

namespace {
void* s_unused;
constexpr int kIdsKept = 16;
} // namespace

static void __fastcall CampCreateVerify(CCampPages* self) {
    // The records' ids before (the first few are enough).
    std::uint32_t ids[kIdsKept] = {};
    {
        auto* before = reinterpret_cast<CParty*>(LocalPlayerData());
        for (int i = 0; i < before->members_.size && i < kIdsKept; ++i) ids[i] = before->members_.data[i].unknown_01C;
    }
    self->Create();
    // No class template for the player's "constr.current": back to the world, nothing built.
    if (!self->constructor_ && self->regions_.size == 0) return;

    bool built = self->regions_.size == 12 && self->sceneObjects_.size == 10 && self->surfaces_.size == 2 &&
                 self->descPanel_ && self->statsPanel_ && self->helpPanel_ && self->itemStrip_ && self->equipment_ &&
                 self->equipment2_ && self->trade_ && self->pageTitleCount_ == 8 && self->turning_ == 0 &&
                 !self->itemMade_ && !self->spellMade_;
    ei::verify::Check("CCampPages::Create built", true, built, "regions %d sprites %d surfaces %d titles %d",
                      self->regions_.size, self->sceneObjects_.size, self->surfaces_.size, self->pageTitleCount_);

    bool offered = (!self->spellConstructor_ || (self->spellStrip_ && self->spellConstructor1_ && self->spellConstructor2_)) &&
                   (!self->itemConstructor_ || (self->partStrip_ && self->itemConstructor1_ && self->itemConstructor2_ &&
                                                self->itemConstructor3_));
    int page = self->itemConstructor_ ? 4 : self->spellConstructor_ ? 2 : 0;
    ei::verify::Check("CCampPages::Create pages", true, offered && self->page_ == page, "spell %d item %d page %d",
                      self->spellConstructor_, self->itemConstructor_, self->page_);

    auto* player = reinterpret_cast<CParty*>(LocalPlayerData());
    bool heroes = self->heroCount_ == player->members_.size;
    for (int i = 0; heroes && i < player->members_.size && i < kIdsKept; ++i)
        heroes = player->members_.data[i].unknown_01C == ids[i];
    ei::verify::Check("CCampPages::Create heroes", true,
                      heroes && self->hero_ >= 0 && self->hero_ < self->heroCount_ && self->heroes_[self->hero_] != nullptr,
                      "heroes %d party %d shown %d", self->heroCount_, player->members_.size, self->hero_);

    if (self->constructor_)
        ei::verify::Check("CCampPages::Create stock", 0, static_cast<int>(self->constructor_->flag38), "");
}
EI_VERIFY(0x0060C870, CampCreateVerify, 0, s_unused);
