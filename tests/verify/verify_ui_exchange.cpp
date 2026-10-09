// Checks for the exchange between players (ui_exchange.cpp). Multiplayer only: no scenario reaches
// it (the harness has no second player).
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
