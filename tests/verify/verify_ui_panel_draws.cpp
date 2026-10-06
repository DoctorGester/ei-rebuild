// Checks for the panels' drawing slots (ui_panel_draws.cpp). They draw (Direct3D clears, models,
// surface text), so ours runs alone.
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/unit.h"

namespace {
void* s_unused;
} // namespace

// 00622C10, ours only: returns 1; the highlight frame is shown exactly when the selected cell holds
// one of the hero's entries. Scenario: ui_camp, ui_describe (every camp frame).
static BOOL __fastcall QuickStripDrawVerify(CHeroQuickStrip* self, void*, int pass) {
    BOOL result = self->Draw(pass);
    PartyUnitRecord* hero = ShownHero(self->owner_);
    bool selected = self->selected_ >= 0 && self->selected_ < static_cast<int>(hero->spellCount);
    auto* frame = static_cast<CUiMesh*>(static_cast<void*>(self->sceneObjects_.data[0]));
    ei::verify::Check("CHeroQuickStrip::Draw", true, result == 1 && (frame->visible_ != 0) == selected,
                      "result %d, selected %d of %u, frame %d", result, self->selected_, hero->spellCount,
                      frame->visible_);
    return result;
}
EI_VERIFY(0x00622C10, QuickStripDrawVerify, 0, s_unused);

// 00627C90, ours only: returns 1, the last Direct3D result (the final depth clear) is a success and
// the item list is unchanged. The repair page needs a trader (not in a scenario yet).
static BOOL __fastcall RepairGridDrawVerify(CRepairGrid* self, void*, int pass) {
    void* items = self->items_;
    BOOL result = self->Draw(pass);
    long hr = self->interface_->renderer_->result_;
    ei::verify::Check("CRepairGrid::Draw", true, result == 1 && hr >= 0 && self->items_ == items,
                      "result %d, clear %08lX", result, static_cast<unsigned long>(hr));
    return result;
}
EI_VERIFY(0x00627C90, RepairGridDrawVerify, 0, s_unused);

// 006284A0 / 00628F80, ours only: return 1, the highlight (scene object 4) is hidden again and the
// pulse time did not go back (CheckNear is not needed: the time only grows by the frame time). The
// constructor pages need a constructor record (`screen 5 <N>` with a constructor class; not in a
// scenario yet).
static void CheckMakerDraw(const char* name, CConstructorPage* self, BOOL result, float pulse) {
    auto* highlight = static_cast<CUiMesh*>(static_cast<void*>(self->sceneObjects_.data[4]));
    ei::verify::Check(name, true, result == 1 && highlight->visible_ == 0 && !(self->pulse_ < pulse),
                      "result %d, highlight %d, pulse %f -> %f", result, highlight->visible_, pulse, self->pulse_);
}

static BOOL __fastcall SpellMakerDrawVerify(CConstructorPage* self, void*, int pass) {
    float pulse = self->pulse_;
    BOOL result = self->DrawSpellMaker(pass);
    CheckMakerDraw("CConstructorPage::DrawSpellMaker", self, result, pulse);
    return result;
}
EI_VERIFY(0x006284A0, SpellMakerDrawVerify, 0, s_unused);

static BOOL __fastcall ItemMakerDrawVerify(CConstructorPage* self, void*, int pass) {
    float pulse = self->pulse_;
    BOOL result = self->DrawItemMaker(pass);
    CheckMakerDraw("CConstructorPage::DrawItemMaker", self, result, pulse);
    return result;
}
EI_VERIFY(0x00628F80, ItemMakerDrawVerify, 0, s_unused);