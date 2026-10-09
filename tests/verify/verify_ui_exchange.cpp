// Checks for the exchange between players (ui_exchange.cpp) and the icon window's bars (005F2E00,
// which only the exchange calls). Multiplayer only: no scenario reaches them (the harness has no
// second player).
//
// 0062D210: ours only (it moves items and leaves the screen): the local party's money is
// partyB's - partyA's + partyC's (read before), and partyB and partyC are empty afterwards.
// 0062C8F0 / 0062C920 / 0062C930 / 0062CF70: ours only (they send to the server, free and move
// items): after Close nothing is described; after Update the money field's change flag is clear
// (or the offer was gone and our offer emptied); the left button: no invariant beyond running.
#include "verify.h"

#include "../../src/game/party.h"
#include "../../src/game/ui_item_info.h"
#include "../../src/game/ui_screens.h"
#include "../../src/game/world.h"

namespace {
void* s_unused;
} // namespace

static void __fastcall ExchangeDoneVerify(CExchangeScreen* self, void*, CParty* partyB, CParty* partyA, CParty* partyC) {
    int expected = partyB->Money() - partyA->Money() + partyC->Money();
    self->OnExchangeDone(partyB, partyA, partyC);
    int money = reinterpret_cast<CParty*>(LocalPlayerData())->Money();
    bool ok = money == expected && partyB->items_.size == 0 && partyC->items_.size == 0;
    ei::verify::Check("CExchangeScreen::OnExchangeDone", true, ok, "money %d/%d left %d %d", expected, money,
                      partyB->items_.size, partyC->items_.size);
}
EI_VERIFY(0x0062D210, ExchangeDoneVerify, 0, s_unused);

static void __fastcall ExchangeCloseVerify(CExchangeScreen* self) {
    self->Close();
    ei::verify::Check("CExchangeScreen::Close", true, self->descPanel_->Item() == nullptr, "");
}
EI_VERIFY(0x0062C8F0, ExchangeCloseVerify, 0, s_unused);

static void __fastcall ExchangeMessageVerify(CExchangeScreen* self, void*, int result) {
    self->OnMessageResult(result);
    ei::verify::Check("CExchangeScreen::OnMessageResult", true, true, "");
}
EI_VERIFY(0x0062C920, ExchangeMessageVerify, 0, s_unused);

static void __fastcall ExchangeUpdateVerify(CExchangeScreen* self) {
    self->Update();
    bool ok = !self->moneyField_->changed_ || self->ownOffer_->items_.size == 0;
    ei::verify::Check("CExchangeScreen::Update", true, ok, "changed %d", self->moneyField_->changed_);
}
EI_VERIFY(0x0062C930, ExchangeUpdateVerify, 0, s_unused);

static void __fastcall ExchangeLButtonDownVerify(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CExchangeScreen>(input)->OnLButtonDown(buttons, x, y);
    ei::verify::Check("CExchangeScreen::OnLButtonDown", true, true, "");
}
EI_VERIFY(0x0062CF70, ExchangeLButtonDownVerify, 0, s_unused);

// 0062C250: ours only (it builds the window's children and surfaces). Afterwards three regions,
// four surfaces, every child made, the scratch party and an emptied offer of ours.
static void __fastcall ExchangeCreateVerify(CExchangeScreen* self) {
    self->Create();
    bool ok = self->regions_.size == 3 && self->surfaces_.size == 4 && self->descPanel_ && self->strip_ &&
              self->figures_ && self->grid_ && self->moneyField_ && self->scratch_ && self->ownOffer_->items_.size == 0;
    ei::verify::Check("CExchangeScreen::Create", true, ok, "regions %d surfaces %d", self->regions_.size,
                      self->surfaces_.size);
}
EI_VERIFY(0x0062C250, ExchangeCreateVerify, 0, s_unused);

// 0062C9C0: ours only (it describes, draws on the surfaces and the children); returns 1 with every
// surface's DC let go.
static BOOL __fastcall ExchangeDrawVerify(CExchangeScreen* self, void*, int pass) {
    BOOL result = self->Draw(pass);
    bool released = true;
    for (int i = 0; i < self->surfaces_.size; ++i) released = released && self->surfaces_.data[i]->dc_ == nullptr;
    ei::verify::Check("CExchangeScreen::Draw", true, result == 1 && released, "result %d", result);
    return result;
}
EI_VERIFY(0x0062C9C0, ExchangeDrawVerify, 0, s_unused);

// 005F2E00 (ui_item_screen.cpp): ours only (the sprites' geometry through the Rebirth-patched
// SetRectangle); the two bars stay where they were in the list.
static void __fastcall IconBarsVerify(CItemIconWindow* self, void*, int y) {
    CEngineObject* left = self->sceneObjects_.data[25];
    CEngineObject* right = self->sceneObjects_.data[26];
    self->PlaceBars(y);
    ei::verify::Check("CItemIconWindow::PlaceBars", true,
                      self->sceneObjects_.data[25] == left && self->sceneObjects_.data[26] == right, "");
}
EI_VERIFY(0x005F2E00, IconBarsVerify, 0, s_unused);
