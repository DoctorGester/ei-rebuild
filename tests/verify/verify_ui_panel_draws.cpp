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
    auto* frame = static_cast<CUiMesh*>(static_cast<void*>(self->sceneObjects_.data_[0]));
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
