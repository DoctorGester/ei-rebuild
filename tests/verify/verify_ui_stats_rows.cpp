// Checks for the statistics panel's row painters (ui_stats_rows.cpp). They only draw text into
// the panel's surface, so ours run alone: the panel's page and points stay; the part's armour is
// recomputed here a second way (each layer sum as a float, the first six types' maximum, never
// below 0). Scenarios ui_camp / ui_describe (the camp's equipment page draws the body parts and
// the rows) and ui_hero_screen.
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/unit.h"

namespace {

void* s_unused;

float ExpectedArmour(CHeroStatsPanel* self, int part) {
    using Fn = PartyUnitRecord*(__thiscall*)(CUiWindow*);
    PartyUnitRecord* hero = reinterpret_cast<Fn>(self->owner_->vtable_[51])(self->owner_);
    const UnitStatSlot& slot = reinterpret_cast<UnitStats*>(hero->stats)->slots[part];
    float best = 0.0f;
    for (int type = 0; type < 6; ++type) {
        volatile float sum = static_cast<float>(static_cast<double>(slot.layers[0].absorb[type]) +
                                                slot.layers[1].absorb[type] + slot.layers[2].absorb[type]);
        if (sum > best) best = sum;
    }
    return best;
}

} // namespace

static void __fastcall StatsPanelDrawRowVerify(CHeroStatsPanel* self, void*, RECT* rect, const char* label, int value,
                                               int maximum) {
    int page = self->page_, points = self->points_;
    self->DrawRow(rect, label, value, maximum);
    ei::verify::Check("CHeroStatsPanel::DrawRow", true, self->page_ == page && self->points_ == points, "%s",
                      label ? label : "(null)");
}
EI_VERIFY(0x006242E0, StatsPanelDrawRowVerify, 0, s_unused);

static void __fastcall StatsPanelDrawPartVerify(CHeroStatsPanel* self, void*, RECT* rect, const char* name, int part) {
    self->DrawPart(rect, name, part);
    ei::verify::CheckNear("CHeroStatsPanel::PartArmour", ExpectedArmour(self, part), self->PartArmour(part), "part %d",
                          part);
}
EI_VERIFY(0x00624590, StatsPanelDrawPartVerify, 0, s_unused);
