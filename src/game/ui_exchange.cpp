// The exchange between players (screen 9, CExchangeScreen; multiplayer only). See ui_screens.h and
// docs/notes/ui.md, section 4.
#include "net_classes.h"
#include "net_messages.h"
#include "party.h"
#include "ui_camp.h"
#include "ui_screens.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_library.h"
#include "originals_ui.h"

namespace {
const char* const kBuySound = reinterpret_cast<const char*>(0x00793800);       // "buttons\camp\buy.wav"
const char* const kCancelSound = reinterpret_cast<const char*>(0x007937E4);    // "buttons\messbox\cancel.wav"
const char* const kTransitSound = reinterpret_cast<const char*>(0x0079372C);   // "buttons\globalmap\transit.wav"
constexpr int kAcceptRegion = 0, kTakeBackRegion = 1, kLeaveRegion = 2;

// The client's own offer (+0x260C) while it is live (not detached), else null.
net::PartyMessage* LiveOffer() {
    auto* offer = static_cast<net::PartyMessage*>(net::GameClient()->party_.ptr);
    return offer && !(offer->flags7_ & CRefObject::kDetached) ? offer : nullptr;
}
CParty* LocalParty() { return reinterpret_cast<CParty*>(LocalPlayerData()); }
} // namespace

// --- 0062C8F0 / 0062C920: closing, message box answers -----------------------------------------------

void CExchangeScreen::Close() {
    InfoPanelDescribeOriginal(descPanel_, nullptr, nullptr);
    delete scratch_;   // slot 0 with 1 (the game's deleting destructor)
}
EI_REPLACE_METHOD(0x0062C8F0, CExchangeScreen::Close);

void CExchangeScreen::OnMessageResult(int) {}
EI_REPLACE_METHOD(0x0062C920, CExchangeScreen::OnMessageResult);

// --- 0062C930: the update (slot 24) ----------------------------------------------------------------

void CExchangeScreen::Update() {
    UiUpdateChildrenOriginal(this);
    if (!LiveOffer()) {
        ItemScreenReturnAllOriginal(this, nullptr, LocalParty(), ownOffer_);
        GlueBackToWorldOriginal(UiClientGlue());
        return;
    }
    bool changed = moneyField_->changed_;
    moneyField_->changed_ = false;
    if (!changed) return;
    ownOffer_->Money() = GameAtoi(moneyField_->text_);
    ClientPublishPartyOriginal(net::GameClient());
}
EI_REPLACE_METHOD(0x0062C930, CExchangeScreen::Update);

// --- 0062CF70: the left button (input slot 2) --------------------------------------------------------

void CExchangeScreen::OnLButtonDown(int buttons, int x, int y) {
    InputOf(this)->BaseLButtonDown(buttons, x, y);
    switch (RegionAt(x, y)) {
    case kAcceptRegion: {
        net::PartyMessage* offer = LiveOffer();
        if (offer && !offer->accepted_ && ownOffer_->Money() <= LocalParty()->Money()) {
            uiorig::PlayUiSound(interface_, kBuySound, 0);
            ClientRecordTradeOriginal(net::GameClient(), nullptr, LocalParty());
            return;
        }
        uiorig::PlayUiSound(interface_, kCancelSound, 0);
        return;
    }
    case kTakeBackRegion: {
        net::PartyMessage* offer = LiveOffer();
        if (offer && offer->accepted_) {
            uiorig::PlayUiSound(interface_, kCancelSound, 0);
            ClientCommitPartyOriginal(net::GameClient());
        }
        return;
    }
    case kLeaveRegion:
        uiorig::PlayUiSound(interface_, kTransitSound, 0);
        ClientDropPartyOriginal(net::GameClient());
        return;
    default:
        return;
    }
}
namespace {
void __fastcall ExchangeLButtonDown(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CExchangeScreen>(input)->OnLButtonDown(buttons, x, y);
}
} // namespace
EI_REPLACE(0x0062CF70, ExchangeLButtonDown);

// --- 0062D210: the exchange went through (slot 37) -------------------------------------------------

void CExchangeScreen::OnExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC) {
    PartyReleaseItemsOriginal(LocalPlayerData());
    auto* local = reinterpret_cast<CParty*>(LocalPlayerData());
    ItemScreenReturnAllOriginal(this, nullptr, local, partyC);
    ItemScreenReturnAllOriginal(this, nullptr, reinterpret_cast<CParty*>(LocalPlayerData()), partyB);
    int money = partyB->Money() - partyA->Money() + partyC->Money();
    reinterpret_cast<CParty*>(LocalPlayerData())->Money() = money;
    GlueBackToWorldOriginal(UiClientGlue());
}
EI_REPLACE_METHOD(0x0062D210, CExchangeScreen::OnExchangeDone);
