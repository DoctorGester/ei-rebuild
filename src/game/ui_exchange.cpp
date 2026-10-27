// The exchange between players (screen 9, CExchangeScreen; multiplayer only). See ui_screens.h and
// docs/notes/ui.md, section 4.
#include "item_db_text.h"
#include "game_strings.h"
#include "net_classes.h"
#include "net_messages.h"
#include "party.h"
#include "ui.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "world_glue.h"
#include "game_data.h"
#include "lib/mfc_slots.h"

#include <cstdint>
#include <cstdlib>

namespace {
const char* const kBuySound = "buttons\\camp\\buy.wav";
const char* const kCancelSound = "buttons\\messbox\\cancel.wav";
const char* const kTransitSound = "buttons\\globalmap\\transit.wav";
constexpr int kAcceptRegion = 0, kTakeBackRegion = 1, kLeaveRegion = 2;

// The client's own offer while it is live (not detached), else null.
net::PartyMessage* LiveOffer() {
    net::PartyMessage* offer = net::GameClient()->party_.Get();
    return IsLive(offer) ? offer : nullptr;
}
CParty* LocalParty() { return LocalPlayerData(); }
} // namespace

// --- closing, message box answers ----------------------------------------------------------------

void CExchangeScreen::Close() {
    descPanel_->Describe(nullptr);
    delete scratch_;   // the deleting destructor
}
EI_REWRITTEN(0x0062C8F0, CExchangeScreen::Close);

void CExchangeScreen::OnMessageResult(int) {}
EI_REPLACE_METHOD(0x0062C920, CExchangeScreen::OnMessageResult);

// --- the update ----------------------------------------------------------------------------------

void CExchangeScreen::Update() {
    static_cast<CUiContainer*>(this)->UpdateBase();
    if (!LiveOffer()) {
        ReturnAll(LocalParty(), ownOffer_);
        GameRejoin();
        return;
    }
    bool changed = moneyField_->changed_;
    moneyField_->changed_ = false;
    if (!changed) return;
    ownOffer_->Money() = std::atoi(moneyField_->text_);
    net::ClientPublishParty(net::GameClient());
}
EI_REWRITTEN(0x0062C930, CExchangeScreen::Update);

// --- the left button -------------------------------------------------------------------------------

void CExchangeScreen::OnLButtonDown(int buttons, int x, int y) {
    CUiContainer::LeftDown(buttons, x, y);
    switch (RegionAt(x, y)) {
    case kAcceptRegion: {
        net::PartyMessage* offer = LiveOffer();
        if (offer && !offer->accepted_ && ownOffer_->Money() <= LocalParty()->Money()) {
            uiorig::PlayUiSound(interface_, kBuySound, 0);
            net::GameClient()->RecordTrade(LocalParty());
            return;
        }
        uiorig::PlayUiSound(interface_, kCancelSound, 0);
        return;
    }
    case kTakeBackRegion: {
        net::PartyMessage* offer = LiveOffer();
        if (offer && offer->accepted_) {
            uiorig::PlayUiSound(interface_, kCancelSound, 0);
            net::ClientCommitParty(net::GameClient());
        }
        return;
    }
    case kLeaveRegion:
        uiorig::PlayUiSound(interface_, kTransitSound, 0);
        net::ClientDropParty(net::GameClient());
        return;
    default:
        return;
    }
}
EI_REWRITTEN(0x0062CF70, CExchangeScreen::OnLButtonDown);

// --- the exchange went through -------------------------------------------------------------------

void CExchangeScreen::OnExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC) {
    static_cast<CParty*>(LocalPlayerData())->ReleaseAllItems();
    auto* local = LocalPlayerData();
    ReturnAll(local, partyC);
    ReturnAll(LocalPlayerData(), partyB);
    int money = partyB->Money() - partyA->Money() + partyC->Money();
    LocalPlayerData()->Money() = money;
    GameRejoin();
}
EI_REWRITTEN(0x0062D210, CExchangeScreen::OnExchangeDone);

// --- Create --------------------------------------------------------------------------------------

namespace {

constexpr int kSurfaceFormat = 5;
constexpr int kStripActionTake = 1;
constexpr int kGridMode = 5;

CUiSurface* NewSurface(CInterface3D* ui, int top, int bottom) {
    return new CUiSurface(ui, 0, top, 200, bottom, kSurfaceFormat, 1);
}

CUiContainer* AsContainer(CExchangeScreen* screen) { return screen; }

} // namespace

void CExchangeScreen::CreateScreen() {
    LoadTexture("campinfo", 3, nullptr);
    CUiRegion* region = AddRegion();
    region->SetRect(12, 450, 52, 490);
    region->SetTip(0xA028, 1, 0);
    region = AddRegion();
    region->SetRect(148, 450, 188, 490);
    region->SetTip(0xA029, 1, 0);
    region = AddRegion();
    region->SetRect(740, 460, 780, 490);
    region->SetTip(0xA02B, 1, 0);
    // The buttons: accept, take back, a hidden one, leave (shaded).
    AddSprite(nullptr)->SetRectangle(3.0f, 100.0f, 12.0f, 450.0f, 52.0f, 490.0f, 0xA5, 0xAC, 0xCD, 0xD4);
    AddSprite(nullptr)->SetRectangle(3.0f, 100.0f, 148.0f, 450.0f, 188.0f, 490.0f, 0xA5, 0xD6, 0xCD, 0xFE);
    CUiMesh* sprite = AddSprite(nullptr);
    sprite->SetRectangle(3.0f, 100.0f, 12.0f, 150.0f, 52.0f, 190.0f, 0xA5, 0xAC, 0xCD, 0xD4);
    sprite->visible_ = 0;
    sprite = AddSprite(nullptr);
    sprite->SetRectangle(3.0f, 100.0f, 740.0f, 460.0f, 780.0f, 490.0f, 0x7D, 0xE0, 0xA5, 0xFE);
    sprite->tint_[0] = 0.5f;
    sprite->tint_[1] = 0.5f;
    sprite->tint_[2] = 0.5f;
    CreateBase();
    iconWindow_->PlaceBars(300);

    // The scratch party (not checked), our offer cleared, the partner's.
    scratch_ = new CParty();
    scratch_->Create();
    net::ClientSession* client = net::GameClient();
    ownOffer_ = client->parties_[0].Get();
    partnerOffer_ = client->parties_[1].Get();
    ownOffer_->Clear(1, 0);

    // The description panel (its item list and text arrays constructed in place).
    auto* panel = new CItemInfoPanel(interface_, this);
    descPanel_ = panel;
    AsContainer(this)->AddChild(panel);
    descPanel_->Build(600, 100);

    // The local party's items (the strip that hands clicks to this screen).
    CUiItemStrip* strip = new CForwardingStrip(interface_, this);
    strip_ = strip;
    AsContainer(this)->AddChild(strip);
    strip_->Setup(&LocalParty()->items_, 0);
    strip_->mode_ = kStripActionTake;
    strip_->SetFilter(0);

    // The players.
    figures_ = new CFigureStrip(interface_, this);
    AsContainer(this)->AddChild(figures_);
    figures_->SetupFigures(&names_, 1, 1);

    // Both offers.
    grid_ = new CTradeGrid(interface_, this);
    AsContainer(this)->AddChild(grid_);
    grid_->Build(&partnerOffer_->items_, &ownOffer_->items_, 0, 1);
    grid_->mode_ = kGridMode;

    // The money offered.
    moneyField_ = new CUiEditField(interface_, RECT{0, 0x15E, 200, 0x16E});
    AsContainer(this)->AddChild(moneyField_);
    moneyField_->SetLook(1, static_cast<char>(1), 1, 9, 0xFFFFFF, 0x31B3FF);
    moneyField_->SetText("");

    AddSurface(NewSurface(interface_, 0x19A, 0x1C2));
    AddSurface(NewSurface(interface_, 0x136, 0x15E));
    AddSurface(NewSurface(interface_, 0xD2, 0x10E));
    AddSurface(NewSurface(interface_, 0x6E, 0xAA));
}
EI_REWRITTEN(0x0062C250, CExchangeScreen::CreateScreen);

// --- Draw ----------------------------------------------------------------------------------------

namespace {
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kGrey = 0xA7D7E4;
constexpr int kAlignLeft = 1;     // DrawText's format here
constexpr int kWrapLines = 2;

void SetTint(CUiMesh* sprite, float value) {
    sprite->tint_[0] = value;
    sprite->tint_[1] = value;
    sprite->tint_[2] = value;
}
CUiMesh* Sprite(CEngineObject* object) { return AsUiMesh(object); }
bool LiveAccepted(const net::PartyMessage* offer) {
    return offer && !(offer->flags7_ & CRefObject::kDetached) && offer->accepted_;
}
} // namespace

BOOL CExchangeScreen::Draw(CSceneRenderer* pass) {
    // What is under the mouse: an item of the strip or else of the grid, or a help topic.
    hoveredItem_ = nullptr;
    int topic = 0;
    hoveredItem_ = strip_->ItemAt(MouseScreenX(), MouseScreenY(), &topic);
    if (!hoveredItem_ && !topic)
        hoveredItem_ = grid_->ItemAt(MouseScreenX(), MouseScreenY(), &topic);
    if (hoveredItem_) {
        if (descPanel_) descPanel_->Describe(hoveredItem_);
    } else if (topic) {
        if (descPanel_) descPanel_->ShowHelp(topic);
    } else if (descPanel_) {
        descPanel_->Describe(nullptr);
    }

    CTextManager* texts = GameClientTexts();
    mfc::CString text, line;
    net::ClientSession* client = net::GameClient();

    // Surface 0: our money and the accept caption.
    CUiSurface* surface = surfaces_.data[0];
    UiSurfaceGetDC(surface);
    surface->Clear();
    texts->GetString("xch_current_money", &text);
    line.Format("%s %d", text.GetString(), LocalParty()->Money());
    RECT rect{0, 0, 200, 0x14};
    surface->DrawTextIn(line, &rect, kAlignLeft, kWhite, 1, 1);
    texts->GetString("xch_accept", &text);
    rect = RECT{0, 0x14, 200, 0x28};
    surface->DrawTextIn(text, &rect, kAlignLeft, kWhite, 1, 1);
    UiSurfaceReleaseDC(surface);

    // Surface 1: the money offer's caption.
    surface = surfaces_.data[1];
    UiSurfaceGetDC(surface);
    surface->Clear();
    texts->GetString("xch_money_offer", &text);
    rect = RECT{0x14, 0, 0xB4, 0x14};
    surface->DrawWrappedText(text, &rect, kWrapLines, 1, kWhite, 1, 1);
    UiSurfaceReleaseDC(surface);

    // Surface 2: the partner's money, while its offer pairs with ours.
    surface = surfaces_.data[2];
    UiSurfaceGetDC(surface);
    surface->Clear();
    net::PartyMessage* partner = net::ClientFindParty(client);
    if (partner && !(partner->flags7_ & CRefObject::kDetached)) {
        texts->GetString("xch_money_remote", &text);
        rect = RECT{0x14, 0, 0xB4, 0x14};
        surface->DrawWrappedText(text, &rect, kWrapLines, 1, kWhite, 1, 1);
        line.Format("%d", partnerOffer_->Money());
        rect = RECT{0, 0x28, 200, 0x3C};
        surface->DrawTextIn(line, &rect, kAlignLeft, kWhite, 1, 1);
    }
    UiSurfaceReleaseDC(surface);

    // Surface 3: whether the partner accepted.
    surface = surfaces_.data[3];
    UiSurfaceGetDC(surface);
    surface->Clear();
    partner = net::ClientFindParty(client);
    if (partner && !(partner->flags7_ & CRefObject::kDetached)) {
        texts->GetString("xch_remote_header", &text);
        rect = RECT{0x14, 0, 0xB4, 0x14};
        surface->DrawWrappedText(text, &rect, kWrapLines, 1, kWhite, 1, 1);
        texts->GetString(net::ClientFindParty(client)->accepted_ ? "xch_remote_ok"
                                                              : "xch_remote_cancel",
                         &text);
        rect = RECT{0, 0x28, 200, 0x3C};
        surface->DrawTextIn(text, &rect, kAlignLeft, kGrey, 1, 1);
    }
    UiSurfaceReleaseDC(surface);

    // The accept button greyed and take-back lit while our offer is accepted, else the reverse;
    // the third one lit while the partner accepted.
    bool accepted = LiveAccepted(client->party_.Get());
    SetTint(Sprite(sceneObjects_.data[0]), accepted ? 0.5f : 1.0f);
    SetTint(Sprite(sceneObjects_.data[1]), accepted ? 1.0f : 0.5f);
    partner = net::ClientFindParty(client);
    bool partnerAccepted = partner && !(partner->flags7_ & CRefObject::kDetached) && net::ClientFindParty(client)->accepted_;
    SetTint(Sprite(sceneObjects_.data[2]), partnerAccepted ? 0.5f : 1.0f);

    AsContainer(this)->CUiContainer::Draw(pass);
    return 1;
}
EI_REWRITTEN(0x0062C9C0, CExchangeScreen::Draw);

// --- an item clicked in the strip or the grid ------------------------------------------------------

namespace {
const char* const kPutOnSound = "buttons\\camp\\put_on.wav";
const char* const kPutOffSound = "buttons\\camp\\put_off.wav";
constexpr int kOfferLimit = 8;
} // namespace

bool CExchangeScreen::CanMove(CItem*, int) { return true; }
EI_REWRITTEN(0x0062D200, CExchangeScreen::CanMove);

void CExchangeScreen::MoveOffered(CItem* item, int mode) {
    if (!CanEquip(item, mode)) {   // CanMove here
        uiorig::PlayUiSound(interface_, kCancelSound, 0);
        return;
    }
    if (mode == kStripActionTake) {
        uiorig::PlayUiSound(interface_, kPutOnSound, 0);
        CParty* local = LocalParty();
        MoveItem(item, ownOffer_, local, kOfferLimit, 1, 0);
        net::ClientPublishParty(net::GameClient());
        return;
    }
    if (mode != kGridMode) return;
    const PartyItemArray& offered = ownOffer_->items_;
    for (int i = 0; i < offered.size; ++i) {
        if (offered.data[i] != item) continue;
        uiorig::PlayUiSound(interface_, kPutOffSound, 0);
        CParty* offer = ownOffer_;
        CItem* held = MoveItem(item, LocalParty(), offer, 0, 1, 0);
        strip_->ScrollToItem(held);
        net::ClientPublishParty(net::GameClient());
        return;
    }
}
EI_REWRITTEN(0x0062D100, CExchangeScreen::MoveOffered);
