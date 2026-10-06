// Checks for the briefing / base screen's dialogue start (src/game/ui_briefing_start.cpp).
//
// StartDialogue moves people (server orders, movers), may open the camp or start the phrases: ours
// only. Afterwards the state is 0, 2 or 3; with a dialogue there is a person, and the three facings
// are flat, of length 1 (or 0), the first two along the line between the first two places, the
// third towards their middle. FinishDialogue: ours only, the state it leaves. Scenario
// ui_base_dialogue (a topic of Smith's, its phrases, then the rewards).
#include "verify.h"

#include "../../src/game/ui_briefing.h"

#include <cmath>

namespace {
void* s_unused;

// `direction` is `from` -> `to`, flat and normalised (zero when they share x and y).
void CheckFacing(const char* name, const Vector3& direction, const Vector3& from, const Vector3& to) {
    float x = to.x - from.x;
    float y = to.y - from.y;
    float length = std::sqrt(x * x + y * y);
    if (length != 0.0f) {
        x /= length;
        y /= length;
    }
    ei::verify::Check(name, true, direction.z == 0.0f, "z %g", direction.z);
    ei::verify::CheckNear(name, x, direction.x, "x");
    ei::verify::CheckNear(name, y, direction.y, "y");
}
} // namespace

static void __fastcall StartDialogueVerify(CBriefingScreen* self, void*, bool immediate, bool remember) {
    self->StartDialogue(immediate, remember);
    const char* name = "CBriefingScreen::StartDialogue";
    int state = self->state_;
    ei::verify::Check(name, true, state == 0 || state == 2 || state == 3, "state %d", state);
    if (state == 0) return;
    ei::verify::Check(name, true, self->person_ != nullptr && self->person_ != self->leader_, "person");
    const Vector3* places = self->places_;
    Vector3 middle{(places[0].x + places[1].x) * 0.5f, (places[0].y + places[1].y) * 0.5f, 0.0f};
    CheckFacing("CBriefingScreen::StartDialogue facing 0", self->directions_[0], places[0], places[1]);
    CheckFacing("CBriefingScreen::StartDialogue facing 1", self->directions_[1], places[1], places[0]);
    CheckFacing("CBriefingScreen::StartDialogue facing 2", self->directions_[2], places[2], middle);
}
EI_VERIFY(0x006064E0, StartDialogueVerify, 0, s_unused);

// FinishDialogue (server orders, people moved, variables completed): ours only. Afterwards state 0,
// no speakers, no shown items, no third speaker or reward unit, briefingStarted_ cleared.
static void __fastcall FinishDialogueVerify(CBriefingScreen* self, void*) {
    self->FinishDialogue();
    bool ok = self->state_ == 0 && self->speakers_ == self->speakersEnd_ && self->shown_->next == self->shown_ &&
              self->shown_->prev == self->shown_ && !self->third_ && !self->rewardUnit_ && !self->briefingStarted_;
    ei::verify::Check("CBriefingScreen::FinishDialogue", true, ok, "state %d", self->state_);
}
EI_VERIFY(0x00608450, FinishDialogueVerify, 0, s_unused);
