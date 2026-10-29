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
    net::PartyMessage* offer = net::g_gameClient->party.Get();
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
    sprite->tint[0] = value;
    sprite->tint[1] = value;
    sprite->tint[2] = value;
}
// An offer that is live (not detached) and accepted.
bool LiveAccepted(const net::PartyMessage* offer) { return IsLive(offer) && offer->accepted; }

const char* const kPutOnSound = "buttons\\camp\\put_on.wav";
const char* const kPutOffSound = "buttons\\camp\\put_off.wav";
constexpr int kOfferLimit = 8;

} // namespace

// --- closing, message box answers ----------------------------------------------------------------

void CExchangeScreen::CloseScreen() {
    descPanel->Describe(nullptr);
    delete scratch;    // the deleting destructor
}

// --- the update ----------------------------------------------------------------------------------

void CExchangeScreen::Update() {
    CUiContainer::Update();
    if (!LiveOffer()) {
        ReturnAll(LocalPlayerData(), ownOffer);
        GameRejoin();
        return;
    }
    bool changed = moneyField->changed;
    moneyField->changed = false;
    if (!changed) return;
    ownOffer->money = str::ToInt(moneyField->text);
    net::ClientPublishParty(net::g_gameClient);
}

// --- the left button -------------------------------------------------------------------------------

void CExchangeScreen::LeftDown(int buttons, int x, int y) {
    CUiContainer::LeftDown(buttons, x, y);
    switch (RegionAt(x, y)) {
    case kAcceptRegion: {
        net::PartyMessage* offer = LiveOffer();
        if (offer && !offer->accepted && ownOffer->money <= LocalPlayerData()->money) {
            ui->PlaySound(kBuySound);
            net::g_gameClient->RecordTrade(LocalPlayerData());
            return;
        }
        ui->PlaySound(kCancelSound);
        return;
    }
    case kTakeBackRegion: {
        net::PartyMessage* offer = LiveOffer();
        if (offer && offer->accepted) {
            ui->PlaySound(kCancelSound);
            net::ClientCommitParty(net::g_gameClient);
        }
        return;
    }
    case kLeaveRegion:
        ui->PlaySound(kTransitSound);
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
    local->money = partyB->money - partyA->money + partyC->money;
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
    sprite->visible = false;
    sprite = AddBlankSprite();
    sprite->SetRectangle(3.0f, 100.0f, 740.0f, 460.0f, 780.0f, 490.0f, 0x7D, 0xE0, 0xA5, 0xFE);
    sprite->tint[0] = 0.5f;
    sprite->tint[1] = 0.5f;
    sprite->tint[2] = 0.5f;
    CreateBase();
    iconWindow->PlaceBars(300);

    // The scratch party (not checked), our offer cleared, the partner's.
    scratch = new CParty();
    scratch->Create();
    net::ClientSession* client = net::g_gameClient;
    ownOffer = client->parties[0].Get();
    partnerOffer = client->parties[1].Get();
    ownOffer->Clear(1, 0);

    // The description panel (its item list and text arrays constructed in place).
    descPanel = new CItemInfoPanel(ui, this);
    AddChild(descPanel);
    descPanel->Build(600, 100);

    // The local party's items (the strip that hands clicks to this screen).
    strip = new CForwardingStrip(ui, this);
    AddChild(strip);
    strip->Setup(&LocalPlayerData()->items, false);
    strip->mode = kStripActionTake;
    strip->SetFilter(0);

    // The players.
    figures = new CFigureStrip(ui, this);
    AddChild(figures);
    figures->SetupFigures(&names, true, true);

    // Both offers.
    grid = new CTradeGrid(ui, this);
    AddChild(grid);
    grid->Build(&partnerOffer->items, &ownOffer->items, false, true);
    grid->mode = kGridMode;

    // The money offered.
    moneyField = new CUiEditField(ui, RECT{0, 0x15E, 200, 0x16E});
    AddChild(moneyField);
    moneyField->SetLook(1, static_cast<char>(1), 1, 9, 0xFFFFFF, 0x31B3FF);
    moneyField->SetText("");

    AddSurface(NewSurface(ui, 0x19A, 0x1C2));
    AddSurface(NewSurface(ui, 0x136, 0x15E));
    AddSurface(NewSurface(ui, 0xD2, 0x10E));
    AddSurface(NewSurface(ui, 0x6E, 0xAA));
}

// --- Draw ----------------------------------------------------------------------------------------

void CExchangeScreen::Draw(CSceneRenderer* pass) {
    // What is under the mouse: an item of the strip or else of the grid, or a help topic.
    hoveredItem = nullptr;
    int topic = 0;
    hoveredItem = strip->ItemAt(g_cursor.position.x, g_cursor.position.y, &topic);
    if (!hoveredItem && !topic)
        hoveredItem = grid->ItemAt(g_cursor.position.x, g_cursor.position.y, &topic);
    if (hoveredItem) {
        if (descPanel) descPanel->Describe(hoveredItem);
    } else if (topic) {
        if (descPanel) descPanel->ShowHelp(topic);
    } else if (descPanel) {
        descPanel->Describe(nullptr);
    }

    CTextManager* texts = g_clientWorld.texts;
    std::string text, line;
    net::ClientSession* client = net::g_gameClient;

    // Surface 0: our money and the accept caption.
    CUiSurface* surface = surfaces[0];
    surface->Clear();
    text = texts->GetString("xch_current_money");
    line = std::format("{} {}", text, LocalPlayerData()->money);
    RECT area{0, 0, 200, 0x14};
    surface->DrawTextIn(line, &area, kAlignLeft, kWhite, 1, 1);
    text = texts->GetString("xch_accept");
    area = RECT{0, 0x14, 200, 0x28};
    surface->DrawTextIn(text, &area, kAlignLeft, kWhite, 1, 1);

    // Surface 1: the money offer's caption.
    surface = surfaces[1];
    surface->Clear();
    text = texts->GetString("xch_money_offer");
    area = RECT{0x14, 0, 0xB4, 0x14};
    surface->DrawWrappedText(text, &area, kWrapLines, 1, kWhite, 1, 1);

    // Surface 2: the partner's money, while its offer pairs with ours.
    surface = surfaces[2];
    surface->Clear();
    net::PartyMessage* partner = net::ClientFindParty(client);
    if (IsLive(partner)) {
        text = texts->GetString("xch_money_remote");
        area = RECT{0x14, 0, 0xB4, 0x14};
        surface->DrawWrappedText(text, &area, kWrapLines, 1, kWhite, 1, 1);
        line = std::format("{}", partnerOffer->money);
        area = RECT{0, 0x28, 200, 0x3C};
        surface->DrawTextIn(line, &area, kAlignLeft, kWhite, 1, 1);
    }

    // Surface 3: whether the partner accepted.
    surface = surfaces[3];
    surface->Clear();
    partner = net::ClientFindParty(client);
    if (IsLive(partner)) {
        text = texts->GetString("xch_remote_header");
        area = RECT{0x14, 0, 0xB4, 0x14};
        surface->DrawWrappedText(text, &area, kWrapLines, 1, kWhite, 1, 1);
        text = texts->GetString(net::ClientFindParty(client)->accepted ? "xch_remote_ok"
                                                              : "xch_remote_cancel");
        area = RECT{0, 0x28, 200, 0x3C};
        surface->DrawTextIn(text, &area, kAlignLeft, kGrey, 1, 1);
    }

    // The accept button greyed and take-back lit while our offer is accepted, else the reverse;
    // the third one lit while the partner accepted.
    bool accepted = LiveAccepted(client->party.Get());
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
        ui->PlaySound(kPutOnSound);
        MoveItem(item, ownOffer, LocalPlayerData(), kOfferLimit, true, false);
        net::ClientPublishParty(net::g_gameClient);
        return;
    }
    if (mode != kGridMode) return;
    const std::vector<CItem*>& offered = ownOffer->items;
    for (int i = 0; i < std::ssize(offered); ++i) {
        if (offered[i] != item) continue;
        ui->PlaySound(kPutOffSound);
        CItem* held = MoveItem(item, LocalPlayerData(), ownOffer, 0, true, false);
        strip->ScrollToItem(held);
        net::ClientPublishParty(net::g_gameClient);
        return;
    }
}
