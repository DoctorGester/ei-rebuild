// Checks for ui_objectives_input.cpp (A-H17; the keys A-H21). Scenario: ui_objectives (TAB with the quest q0g held,
// then clicks on the window's regions).
// - 0064EA80 FillQuestPlaces: original first (the places it leaves are kept), then ours (which
//   empties the vector first), the points compared bit for bit. Both open and close map.txt in the
//   text manager and look the quest up through operator[], so the second run starts from the same
//   state.
// - 0064E330 OnLButtonDown: ours only (sounds, the pending value, zone and quest changes): the zone
//   index stays inside the zone list, the selected quest inside the quest list or unchanged.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_screens.h"

#include <cstring>
#include <vector>

namespace {
void* s_unused;
void(__fastcall* s_fillQuestPlaces)(CObjectivesWindow*, void*, const char*);

void __fastcall FillQuestPlacesVerify(CObjectivesWindow* self, void*, const char* name) {
    s_fillQuestPlaces(self, nullptr, name);
    std::vector<UiPointF> expected(self->questPlaces_.begin(), self->questPlaces_.end());
    self->LoadQuestPlaces(name);
    stlp::vector<UiPointF>& places = self->questPlaces_;
    bool ok = places.size() == expected.size() &&
              (expected.empty() || std::memcmp(places.begin(), expected.data(), expected.size() * sizeof(UiPointF)) == 0);
    ei::verify::Check("CObjectivesWindow::LoadQuestPlaces", true, ok, "%s: %u places vs %u", name,
                      static_cast<unsigned>(places.size()), static_cast<unsigned>(expected.size()));
}

void __fastcall LButtonDownVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CObjectivesWindow>(input);
    int selected = self->selectedQuest_;
    self->OnLButtonDown(buttons, x, y);
    int zones = static_cast<int>(self->zones_.end() - self->zones_.begin());
    int quests = self->quests_.GetSize();
    bool ok = (zones == 0 || (self->zoneIndex_ >= 0 && self->zoneIndex_ < zones)) &&
              (self->selectedQuest_ == selected || (self->selectedQuest_ >= 0 && self->selectedQuest_ < quests));
    ei::verify::Check("CObjectivesWindow::OnLButtonDown", true, ok, "(%d, %d): zone %d of %d, quest %d of %d", x, y,
                      self->zoneIndex_, zones, self->selectedQuest_, quests);
}
// 0064E5A0 (A-H21): ours only (sounds, the pending value, zone requests): Enter, Esc and the four
// list keys mark the key used; the list keys move the selection (RefreshQuests may clamp it).
void __fastcall KeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CObjectivesWindow>(input);
    self->OnKeyDown(key, repeat, flags);
    bool used = key == 0x0D || key == 0x1B || key == 0x21 || key == 0x22 || key == 0x26 || key == 0x28;
    ei::verify::Check("CObjectivesWindow::OnKeyDown", true, !used || self->inputHandled_, "key %d: quest %d", key,
                      self->selectedQuest_);
}
} // namespace

// 0064EA80: MOV EAX,FS:[0] (6).
EI_VERIFY(0x0064EA80, FillQuestPlacesVerify, 6, s_fillQuestPlaces);
EI_VERIFY(0x0064E330, LButtonDownVerify, 0, s_unused);
EI_VERIFY(0x0064E5A0, KeyDownVerify, 0, s_unused);
