// Checks for the multiplayer hero screen's draw, update and close and the trade panel's experience
// figures (ui_hero_screen.cpp). Scenario ui_hero_screen (the hero list and a new hero; the
// statistics page, mode 11, and so DrawExperience and the trade panel's figures, are not reached).
#include "verify.h"

#include "../../src/game/originals_ui.h"
#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/unit.h"

#include <cmath>
#include <cstdint>

namespace {
void* s_unused;
} // namespace

// --- 00625530 / 00625560: pure, side by side ------------------------------------------------------
// Both start with MOV EAX,[ECX+0x150] (6 bytes).

static int(__fastcall* s_experience)(CTradePanel*);
static int __fastcall TradeExperienceVerify(CTradePanel* self) {
    int expected = s_experience(self);
    int actual = self->Experience();
    ei::verify::Check("CTradePanel::Experience", expected, actual, "record %d", self->recordIndex_);
    return expected;
}
EI_VERIFY(0x00625530, TradeExperienceVerify, 6, s_experience);

static int(__fastcall* s_purchaseCost)(CTradePanel*);
static int __fastcall TradePurchaseCostVerify(CTradePanel* self) {
    int expected = s_purchaseCost(self);
    int actual = self->PurchaseCost();
    ei::verify::Check("CTradePanel::PurchaseCost", expected, actual, "record %d", self->recordIndex_);
    return expected;
}
EI_VERIFY(0x00625560, TradePurchaseCostVerify, 6, s_purchaseCost);

// --- 006159E0: Draw (every frame of modes 9 and 10); ours only ---------------------------------------
// It draws and describes, so it runs once: returns 1 and leaves the mode and the selection alone.

static BOOL __fastcall HeroScreenDrawVerify(CMpHeroScreen* self, void*, int pass) {
    int mode = self->mode_;
    int hero = self->heroIndex_;
    BOOL result = self->Draw(pass);
    bool ok = result == 1 && self->mode_ == mode && self->heroIndex_ == hero;
    ei::verify::Check("CMpHeroScreen::Draw", true, ok, "mode %d -> %d result %d", mode, self->mode_, result);
    return result;
}
EI_VERIFY(0x006159E0, HeroScreenDrawVerify, 0, s_unused);

// --- 00617820: Update (every frame); ours only ---------------------------------------------------------
// The preview's turn is recomputed from the frame time (the same within a frame); on the new hero's
// page the create button's tint must follow the attributes and the name.

static void __fastcall HeroScreenUpdateVerify(CMpHeroScreen* self, void*) {
    int rotating = self->rotating_;
    float turn = self->turn_;
    double expectedTurn = turn;
    if (rotating == 1 || rotating == 2) {
        double step = WindowFrameTimeOriginal(&self->window);
        step += step;
        double period = *reinterpret_cast<const volatile float*>(0x007C7F10);
        expectedTurn = std::fmod(rotating == 1 ? step + turn : turn - step, period);
    }
    self->Update();
    ei::verify::CheckNear("CMpHeroScreen::Update turn", static_cast<float>(expectedTurn), self->turn_, "rotating %d",
                          rotating);
    if (self->mode_ == 10) {
        const auto* stats = reinterpret_cast<const UnitStats*>(reinterpret_cast<PartyUnitRecord*>(self->Hero())->stats);
        double points = static_cast<double>(stats->stat20Base) + stats->stat18Base + stats->stat10Base;
        const char* name = static_cast<CUiEditField*>(self->nameEdit_)->text_;
        bool ready = points == *reinterpret_cast<const volatile float*>(0x00741C98) && name && name[0];
        auto* button = reinterpret_cast<CUiMesh*>(self->window.sceneObjects_.data_[9]);
        ei::verify::Check("CMpHeroScreen::Update create button", ready ? 1.0f : 0.5f, button->tint_[0], "points %g",
                          points);
    }
}
EI_VERIFY(0x00617820, HeroScreenUpdateVerify, 0, s_unused);

// --- 00615850: Close; ours only ------------------------------------------------------------------------

static void __fastcall HeroScreenCloseVerify(CMpHeroScreen* self, void*) {
    self->Close();
    ei::verify::Check("CMpHeroScreen::Close", true, self->previewModel_ == nullptr, "preview %p",
                      static_cast<void*>(self->previewModel_));
}
EI_VERIFY(0x00615850, HeroScreenCloseVerify, 0, s_unused);
