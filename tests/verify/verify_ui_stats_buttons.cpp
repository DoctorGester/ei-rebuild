// Checks for the statistics panel's -/+ buttons (ui_stats_buttons.cpp). Both change the hero's
// statistics, the mouse capture and call the owner, so ours run alone; the wrappers check the hold
// state they leave. Scenario ui_hero_stats (the new hero's page: -/+ clicks at the panel's
// regions; the coordinates come from ShowStats' page-10 layout and are not yet confirmed by a run).
#include "verify.h"

#include "../../src/game/originals_ui.h"
#include "../../src/game/ui_camp.h"

namespace {
void* s_unused;
} // namespace

// 00624A60: off page 10 or without a hold nothing changes; with one, the hold either goes on (the
// time advanced by the frame time) or ends (region -1, time 0, the capture cleared).
static void __fastcall StatsPanelUpdateVerify(CHeroStatsPanel* self, void*) {
    int page = self->page_;
    int held = self->heldRegion_;
    float time = self->holdTime_;
    double frame = WindowFrameTimeOriginal(self);
    self->Update();
    const char* name = "CHeroStatsPanel::Update";
    if (page != 10 || held < 0) {
        ei::verify::Check(name, true, self->heldRegion_ == held && self->holdTime_ == time, "idle page %d", page);
        return;
    }
    if (self->heldRegion_ == held) {
        ei::verify::CheckNear(name, static_cast<float>(frame + time), self->holdTime_, "held %d", held);
    } else {
        bool released = self->heldRegion_ == -1 && self->holdTime_ == 0.0f && UiCapture(self->interface_) == nullptr;
        ei::verify::Check(name, true, released, "released %d -> %d", held, self->heldRegion_);
    }
}
EI_VERIFY(0x00624A60, StatsPanelUpdateVerify, 0, s_unused);

// 00624790: on page 10 the held region is -1 or the region clicked (6-13), and a hold starts at
// -0.5 s with the mouse captured by the panel's input side.
static void __fastcall StatsPanelLButtonDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CHeroStatsPanel>(input);
    int region = self->RegionAt(x, y);
    bool page10 = self->page_ == 10;
    self->OnLButtonDown(buttons, x, y);
    if (!page10) return;
    bool ok = self->heldRegion_ == -1 ||
              (self->heldRegion_ == region && region >= 6 && region < 14 && self->holdTime_ == -0.5f &&
               UiCapture(self->interface_) == input);
    ei::verify::Check("CHeroStatsPanel::OnLButtonDown", true, ok, "region %d held %d", region, self->heldRegion_);
}
EI_VERIFY(0x00624790, StatsPanelLButtonDownVerify, 0, s_unused);
