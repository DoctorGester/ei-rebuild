// The exchange between players (screen 9, CExchangeScreen; multiplayer only). See ui_screens.h and
// docs/notes/ui.md, section 4.
#include "text_manager.h"
#include "game_strings.h"
#include "net_classes.h"
#include "net_messages.h"
#include "party.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"
#include "world.h"

#include "lib/crt.h"
#include "world_glue.h"
#include "ui_cursor.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <format>

namespace {
const char* const kBuySound = "buttons\\camp\\buy.wav";
const char* const kCancelSound = "buttons\\messbox\\cancel.wav";
const char* const kTransitSound = "buttons\\globalmap\\transit.wav";
constexpr int kAcceptRegion = 0, kTakeBackRegion = 1, kLeaveRegion = 2;

// The client's own offer while it is live (not detached), else null.
net::PartyMessage* LiveOffer() {
    net::PartyMessage* offer = net::g_gameClient->party_.Get();
    return IsLive(offer) ? offer : nullptr;
}

constexpr int kStripActionTake = 1;
constexpr int kGridMode = 5;

// A text surface of the left column (x 0..200).
CUiSurface* NewSurface(CInterface3D* ui, int top, int bottom) {
    return new CUiSurface(ui, 0, top, 200, bottom, SurfaceBlend::Keyed, true);
}

constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kGrey = 0xA7D7E4;
constexpr int kAlignLeft = 1;     // DrawText's format here
constexpr int kWrapLines = 2;

void SetTint(CUiMesh* sprite, float value) {
    sprite->tint_[0] = value;
    sprite->tint_[1] = value;
    sprite->tint_[2] = value;
}
// An offer that is live (not detached) and accepted.
bool LiveAccepted(const net::PartyMessage* offer) { return IsLive(offer) && offer->accepted_; }

const char* const kPutOnSound = "buttons\\camp\\put_on.wav";
const char* const kPutOffSound = "buttons\\camp\\put_off.wav";
constexpr int kOfferLimit = 8;

} // namespace

// --- closing, message box answers ----------------------------------------------------------------

void CExchangeScreen::CloseScreen() {
    descPanel_->Describe(nullptr);
    delete scratch_;   // the deleting destructor
}

// --- the update ----------------------------------------------------------------------------------

void CExchangeScreen::Update() {
    CUiContainer::Update();
    if (!LiveOffer()) {
        ReturnAll(LocalPlayerData(), ownOffer_);
        GameRejoin();
        return;
    }
    bool changed = moneyField_->changed_;
    moneyField_->changed_ = false;
    if (!changed) return;
    ownOffer_->money_ = str::ToInt(moneyField_->text_);
    net::ClientPublishParty(net::g_gameClient);
}

// --- the left button -------------------------------------------------------------------------------

void CExchangeScreen::LeftDown(int buttons, int x, int y) {
    CUiContainer::LeftDown(buttons, x, y);
    switch (RegionAt(x, y)) {
    case kAcceptRegion: {
        net::PartyMessage* offer = LiveOffer();
        if (offer && !offer->accepted_ && ownOffer_->money_ <= LocalPlayerData()->money_) {
            interface_->PlaySound(kBuySound);
            net::g_gameClient->RecordTrade(LocalPlayerData());
            return;
        }
        interface_->PlaySound(kCancelSound);
        return;
    }
    case kTakeBackRegion: {
        net::PartyMessage* offer = LiveOffer();
        if (offer && offer->accepted_) {
            interface_->PlaySound(kCancelSound);
            net::ClientCommitParty(net::g_gameClient);
        }
        return;
    }
    case kLeaveRegion:
        interface_->PlaySound(kTransitSound);
        net::ClientDropParty(net::g_gameClient);
        return;
    default:
        return;
    }
}

// --- the exchange went through -------------------------------------------------------------------

void CExchangeScreen::ExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC) {
    CParty* local = LocalPlayerData();
    local->ReleaseAllItems();
    ReturnAll(local, partyC);
    ReturnAll(local, partyB);
    local->money_ = partyB->money_ - partyA->money_ + partyC->money_;
    GameRejoin();
}

// --- Create --------------------------------------------------------------------------------------

void CExchangeScreen::CreateScreen() {
    LoadTexture("campinfo", TextureKind::Interface);
    CUiRegion* region = AddRegion();
    region->SetRect(12, 450, 52, 490);
    region->SetTip(0xA028);
    region = AddRegion();
    region->SetRect(148, 450, 188, 490);
    region->SetTip(0xA029);
    region = AddRegion();
    region->SetRect(740, 460, 780, 490);
    region->SetTip(0xA02B);
    // The buttons: accept, take back, a hidden one, leave (shaded).
    AddBlankSprite()->SetRectangle(3.0f, 100.0f, 12.0f, 450.0f, 52.0f, 490.0f, 0xA5, 0xAC, 0xCD, 0xD4);
    AddBlankSprite()->SetRectangle(3.0f, 100.0f, 148.0f, 450.0f, 188.0f, 490.0f, 0xA5, 0xD6, 0xCD, 0xFE);
    CUiMesh* sprite = AddBlankSprite();
    sprite->SetRectangle(3.0f, 100.0f, 12.0f, 150.0f, 52.0f, 190.0f, 0xA5, 0xAC, 0xCD, 0xD4);
    sprite->visible_ = false;
    sprite = AddBlankSprite();
    sprite->SetRectangle(3.0f, 100.0f, 740.0f, 460.0f, 780.0f, 490.0f, 0x7D, 0xE0, 0xA5, 0xFE);
    sprite->tint_[0] = 0.5f;
    sprite->tint_[1] = 0.5f;
    sprite->tint_[2] = 0.5f;
    CreateBase();
    iconWindow_->PlaceBars(300);

    // The scratch party (not checked), our offer cleared, the partner's.
    scratch_ = new CParty();
    scratch_->Create();
    net::ClientSession* client = net::g_gameClient;
    ownOffer_ = client->parties_[0].Get();
    partnerOffer_ = client->parties_[1].Get();
    ownOffer_->Clear(1, 0);

    // The description panel (its item list and text arrays constructed in place).
    descPanel_ = new CItemInfoPanel(interface_, this);
    AddChild(descPanel_);
    descPanel_->Build(600, 100);

    // The local party's items (the strip that hands clicks to this screen).
    strip_ = new CForwardingStrip(interface_, this);
    AddChild(strip_);
    strip_->Setup(&LocalPlayerData()->items_, false);
    strip_->mode_ = kStripActionTake;
    strip_->SetFilter(0);

    // The players.
    figures_ = new CFigureStrip(interface_, this);
    AddChild(figures_);
    figures_->SetupFigures(&names_, true, true);

    // Both offers.
    grid_ = new CTradeGrid(interface_, this);
    AddChild(grid_);
    grid_->Build(&partnerOffer_->items_, &ownOffer_->items_, false, true);
    grid_->mode_ = kGridMode;

    // The money offered.
    moneyField_ = new CUiEditField(interface_, RECT{0, 0x15E, 200, 0x16E});
    AddChild(moneyField_);
    moneyField_->SetLook(1, static_cast<char>(1), 1, 9, 0xFFFFFF, 0x31B3FF);
    moneyField_->SetText("");

    AddSurface(NewSurface(interface_, 0x19A, 0x1C2));
    AddSurface(NewSurface(interface_, 0x136, 0x15E));
    AddSurface(NewSurface(interface_, 0xD2, 0x10E));
    AddSurface(NewSurface(interface_, 0x6E, 0xAA));
}

// --- Draw ----------------------------------------------------------------------------------------

void CExchangeScreen::Draw(CSceneRenderer* pass) {
    // What is under the mouse: an item of the strip or else of the grid, or a help topic.
    hoveredItem_ = nullptr;
    int topic = 0;
    hoveredItem_ = strip_->ItemAt(g_cursor.position_.x, g_cursor.position_.y, &topic);
    if (!hoveredItem_ && !topic)
        hoveredItem_ = grid_->ItemAt(g_cursor.position_.x, g_cursor.position_.y, &topic);
    if (hoveredItem_) {
        if (descPanel_) descPanel_->Describe(hoveredItem_);
    } else if (topic) {
        if (descPanel_) descPanel_->ShowHelp(topic);
    } else if (descPanel_) {
        descPanel_->Describe(nullptr);
    }

    CTextManager* texts = g_clientWorld.texts_;
    std::string text, line;
    net::ClientSession* client = net::g_gameClient;

    // Surface 0: our money and the accept caption.
    CUiSurface* surface = surfaces_[0];
    surface->Clear();
    texts->GetString("xch_current_money", &text);
    line = std::format("{} {}", text, LocalPlayerData()->money_);
    RECT rect{0, 0, 200, 0x14};
    surface->DrawTextIn(line, &rect, kAlignLeft, kWhite, 1, 1);
    texts->GetString("xch_accept", &text);
    rect = RECT{0, 0x14, 200, 0x28};
    surface->DrawTextIn(text, &rect, kAlignLeft, kWhite, 1, 1);

    // Surface 1: the money offer's caption.
    surface = surfaces_[1];
    surface->Clear();
    texts->GetString("xch_money_offer", &text);
    rect = RECT{0x14, 0, 0xB4, 0x14};
    surface->DrawWrappedText(text, &rect, kWrapLines, 1, kWhite, 1, 1);

    // Surface 2: the partner's money, while its offer pairs with ours.
    surface = surfaces_[2];
    surface->Clear();
    net::PartyMessage* partner = net::ClientFindParty(client);
    if (IsLive(partner)) {
        texts->GetString("xch_money_remote", &text);
        rect = RECT{0x14, 0, 0xB4, 0x14};
        surface->DrawWrappedText(text, &rect, kWrapLines, 1, kWhite, 1, 1);
        line = std::format("{}", partnerOffer_->money_);
        rect = RECT{0, 0x28, 200, 0x3C};
        surface->DrawTextIn(line, &rect, kAlignLeft, kWhite, 1, 1);
    }

    // Surface 3: whether the partner accepted.
    surface = surfaces_[3];
    surface->Clear();
    partner = net::ClientFindParty(client);
    if (IsLive(partner)) {
        texts->GetString("xch_remote_header", &text);
        rect = RECT{0x14, 0, 0xB4, 0x14};
        surface->DrawWrappedText(text, &rect, kWrapLines, 1, kWhite, 1, 1);
        texts->GetString(net::ClientFindParty(client)->accepted_ ? "xch_remote_ok"
                                                              : "xch_remote_cancel",
                         &text);
        rect = RECT{0, 0x28, 200, 0x3C};
        surface->DrawTextIn(text, &rect, kAlignLeft, kGrey, 1, 1);
    }

    // The accept button greyed and take-back lit while our offer is accepted, else the reverse;
    // the third one lit while the partner accepted.
    bool accepted = LiveAccepted(client->party_.Get());
    SetTint(SceneMesh(0), accepted ? 0.5f : 1.0f);
    SetTint(SceneMesh(1), accepted ? 1.0f : 0.5f);
    bool partnerAccepted = LiveAccepted(net::ClientFindParty(client));
    SetTint(SceneMesh(2), partnerAccepted ? 0.5f : 1.0f);

    CUiContainer::Draw(pass);
}

// --- an item clicked in the strip or the grid ------------------------------------------------------

// (The original first asks CanEquip, which always accepts here.)
void CExchangeScreen::TakeItem(CItem* item, int mode) {
    if (mode == kStripActionTake) {
        interface_->PlaySound(kPutOnSound);
        MoveItem(item, ownOffer_, LocalPlayerData(), kOfferLimit, true, false);
        net::ClientPublishParty(net::g_gameClient);
        return;
    }
    if (mode != kGridMode) return;
    const std::vector<CItem*>& offered = ownOffer_->items_;
    for (int i = 0; i < std::ssize(offered); ++i) {
        if (offered[i] != item) continue;
        interface_->PlaySound(kPutOffSound);
        CItem* held = MoveItem(item, LocalPlayerData(), ownOffer_, 0, true, false);
        strip_->ScrollToItem(held);
        net::ClientPublishParty(net::g_gameClient);
        return;
    }
}
