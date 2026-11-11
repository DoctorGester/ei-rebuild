// The exchange between players (screen 9, ExchangeScreen; multiplayer only). See ui_screens.h and
// docs/notes/ui.md, section 4.
#include "engine/data/text_manager.h"
#include "game/net/net_session.h"
#include "game/net/net_messages.h"
#include "game/players/party.h"
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_edit_field.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_item_info.h"
#include "game/ui/ui_screens.h"
#include "game/world/world.h"

#include "lib/crt.h"
#include "game/players/player_client.h"
#include "game/ui/ui_cursor.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <format>

namespace {
const char* const kBuySound = "buttons\\camp\\buy.wav";
const char* const kTransitSound = "buttons\\globalmap\\transit.wav";


// The client's own offer while it is live (not detached), else null.
net::PartyMessage* LiveOffer() {
    net::PartyMessage* offer = net::g_gameClient->party.Get();
    return IsLive(offer) ? offer : nullptr;
}

constexpr int kStripActionTake = 1;
constexpr int kGridMode = 5;

constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr int kFont = 1;
constexpr int kWrapLines = 2;

void SetTint(UiMesh* sprite, float value) {
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

void ExchangeScreen::CloseScreen() {
    descPanel->Describe(nullptr);
    delete scratch;    // the deleting destructor
    g_interface.exchange = nullptr;
}

// --- the update ----------------------------------------------------------------------------------

void ExchangeScreen::Update() {
    UiContainer::Update();
    if (!LiveOffer()) {
        ReturnAll(LocalPlayerData(), ownOffer);
        g_interface.closeAll = 1;
        g_clientWorld.LocalView()->Rejoin();
        return;
    }
    bool changed = moneyField->changed;
    moneyField->changed = false;
    if (!changed) return;
    ownOffer->money = str::ToInt(moneyField->text);
    net::g_gameClient->PublishParty();
}

// --- the left button -------------------------------------------------------------------------------

void ExchangeScreen::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    default: ItemScreen::OnEvent(event); break;
    }
}

void ExchangeScreen::LeftDown(UiEvent& event) {
    UiContainer::OnEvent(event);
    UiRegion* region = RegionUnder(event.at);
    if (region == acceptButton) {
        net::PartyMessage* offer = LiveOffer();
        if (offer && !offer->accepted && ownOffer->money <= LocalPlayerData()->money) {
            g_interface.PlayInterfaceSound(kBuySound);
            net::g_gameClient->RecordTrade(LocalPlayerData());
            return;
        }
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
    } else if (region == takeBackButton) {
        net::PartyMessage* offer = LiveOffer();
        if (offer && offer->accepted) {
            g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
            net::g_gameClient->CommitParty();
        }
    } else if (region == leaveButton) {
        g_interface.PlayInterfaceSound(kTransitSound);
        net::g_gameClient->DropParty();
    }
}

// --- the exchange went through -------------------------------------------------------------------

void ExchangeScreen::ExchangeDone(Party* partyB, Party* partyA, Party* partyC) {
    Party* local = LocalPlayerData();
    ReleaseItems(local->items);
    ReturnAll(local, partyC);
    ReturnAll(local, partyB);
    local->money = partyB->money - partyA->money + partyC->money;
    g_interface.closeAll = 1;
    g_clientWorld.LocalView()->Rejoin();
}

// --- Create --------------------------------------------------------------------------------------

void ExchangeScreen::CreateScreen() {
    g_interface.exchange = this;
    LoadTexture("campinfo", TextureKind::Interface);
    acceptButton = AddRegion();
    acceptButton->SetRect(12, 450, 52, 490);
    acceptButton->SetTip(0xA028);
    takeBackButton = AddRegion();
    takeBackButton->SetRect(148, 450, 188, 490);
    takeBackButton->SetTip(0xA029);
    leaveButton = AddRegion();
    leaveButton->SetRect(740, 460, 780, 490);
    leaveButton->SetTip(0xA02B);
    // The buttons: accept, take back, the partner's (hidden), leave (shaded).
    acceptIcon = AddBlankSprite();
    acceptIcon->SetRectangle(kDialogDepth, kSpriteScale, 12.0f, 450.0f, 52.0f, 490.0f, 0xA5, 0xAC, 0xCD, 0xD4);
    takeBackIcon = AddBlankSprite();
    takeBackIcon->SetRectangle(kDialogDepth, kSpriteScale, 148.0f, 450.0f, 188.0f, 490.0f, 0xA5, 0xD6, 0xCD, 0xFE);
    partnerIcon = AddBlankSprite();
    partnerIcon->SetRectangle(kDialogDepth, kSpriteScale, 12.0f, 150.0f, 52.0f, 190.0f, 0xA5, 0xAC, 0xCD, 0xD4);
    partnerIcon->visible = false;
    UiMesh* sprite = AddBlankSprite();
    sprite->SetRectangle(kDialogDepth, kSpriteScale, 740.0f, 460.0f, 780.0f, 490.0f, 0x7D, 0xE0, 0xA5, 0xFE);
    sprite->tint[0] = 0.5f;
    sprite->tint[1] = 0.5f;
    sprite->tint[2] = 0.5f;
    CreateBase();
    iconWindow->PlaceBars(300);

    // The scratch party (not checked), our offer cleared, the partner's.
    scratch = new Party();
    scratch->Create();
    net::ClientSession* client = net::g_gameClient;
    ownOffer = &client->parties[0];
    partnerOffer = &client->parties[1];
    ownOffer->Clear(true, false);

    // The description panel (its item list and text arrays constructed in place).
    descPanel = AddChild(std::make_unique<ItemInfoPanel>(this));
    descPanel->Build(600, 100);

    // The local party's items (the strip that hands clicks to this screen).
    strip = AddChild(std::make_unique<ForwardingStrip>(this));
    strip->Setup(&LocalPlayerData()->items, false);
    strip->mode = kStripActionTake;
    strip->SetFilter(0);

    // The players.
    figures = AddChild(std::make_unique<FigureStrip>(this));
    figures->SetupFigures(&names, true, true);

    // Both offers.
    grid = AddChild(std::make_unique<TradeGrid>(this));
    grid->Build(&partnerOffer->items, &ownOffer->items, false, true);
    grid->mode = kGridMode;

    // The money offered.
    moneyField = AddChild(std::make_unique<UiEditField>(Rect{0, 0x15E, 200, 0x16E}));
    moneyField->SetLook(1, static_cast<char>(1), 1, 9, 0xFFFFFF, 0x31B3FF);
    moneyField->SetText("");

    const int tops[4] = {0x19A, 0x136, 0xD2, 0x6E}, bottoms[4] = {0x1C2, 0x15E, 0x10E, 0xAA};
    for (int i = 0; i < 4; ++i)
        AddSurface(NewSurface(Rect{0, tops[i], 200, bottoms[i]}))->PaintWith([this, i](UiSurface& out) { PaintLabels(out, labels[i]); });
}

// --- Draw ----------------------------------------------------------------------------------------

void ExchangeScreen::DrawSprites(SceneRenderer* pass) {
    // What is under the mouse: an item of the strip or else of the grid, or a help topic.
    ItemUnder under = strip->ItemAt(g_cursor.position);
    if (!under.item && !under.topic) under = grid->ItemAt(g_cursor.position);
    hoveredItem = under.item;
    const int topic = under.topic;
    if (hoveredItem) {
        if (descPanel) descPanel->Describe(hoveredItem);
    } else if (topic) {
        if (descPanel) descPanel->ShowHelp(topic);
    } else if (descPanel) {
        descPanel->Describe(nullptr);
    }

    TextManager* texts = g_clientWorld.texts.get();
    std::string text, line;
    net::ClientSession* client = net::g_gameClient;

    for (std::vector<UiLabel>& surfaceLabels : labels) surfaceLabels.clear();
    // Surface 0: our money and the accept caption.
    text = texts->GetString("xch_current_money");
    line = std::format("{} {}", text, LocalPlayerData()->money);
    labels[0].push_back({line, Rect{0, 0, 200, 0x14}, {kFont, kWhite, render::text::Format::Center}});
    text = texts->GetString("xch_accept");
    labels[0].push_back({text, Rect{0, 0x14, 200, 0x28}, {kFont, kWhite, render::text::Format::Center}});

    // Surface 1: the money offer's caption.
    text = texts->GetString("xch_money_offer");
    labels[1].push_back({text, Rect{0x14, 0, 0xB4, 0x14}, {1, kWhite, render::text::Format::Center}, kWrapLines});

    // Surface 2: the partner's money, while its offer pairs with ours.
    net::PartyMessage* partner = client->FindParty();
    if (IsLive(partner)) {
        text = texts->GetString("xch_money_remote");
        labels[2].push_back({text, Rect{0x14, 0, 0xB4, 0x14}, {1, kWhite, render::text::Format::Center}, kWrapLines});
        line = std::format("{}", partnerOffer->money);
        labels[2].push_back({line, Rect{0, 0x28, 200, 0x3C}, {kFont, kWhite, render::text::Format::Center}});
    }

    // Surface 3: whether the partner accepted.
    partner = client->FindParty();
    if (IsLive(partner)) {
        text = texts->GetString("xch_remote_header");
        labels[3].push_back({text, Rect{0x14, 0, 0xB4, 0x14}, {1, kWhite, render::text::Format::Center}, kWrapLines});
        text = texts->GetString(client->FindParty()->accepted ? "xch_remote_ok"
                                                              : "xch_remote_cancel");
        labels[3].push_back({text, Rect{0, 0x28, 200, 0x3C}, {kFont, kUiTextColour, render::text::Format::Center}});
    }

    // The accept button greyed and take-back lit while our offer is accepted, else the reverse;
    // the third one lit while the partner accepted.
    bool accepted = LiveAccepted(client->party.Get());
    SetTint(acceptIcon, accepted ? 0.5f : 1.0f);
    SetTint(takeBackIcon, accepted ? 1.0f : 0.5f);
    bool partnerAccepted = LiveAccepted(client->FindParty());
    SetTint(partnerIcon, partnerAccepted ? 0.5f : 1.0f);

    UiContainer::DrawSprites(pass);
}

// --- an item clicked in the strip or the grid ------------------------------------------------------

// (The original first asks CanEquip, which always accepts here.)
void ExchangeScreen::TakeItem(Item* item, int mode) {
    if (mode == kStripActionTake) {
        g_interface.PlayInterfaceSound(kPutOnSound);
        MoveItem(item, ownOffer, LocalPlayerData(), kOfferLimit, true, false);
        net::g_gameClient->PublishParty();
        return;
    }
    if (mode != kGridMode) return;
    const std::vector<Item*>& offered = ownOffer->items;
    for (const Item* entry : offered) {
        if (entry != item) continue;
        g_interface.PlayInterfaceSound(kPutOffSound);
        Item* held = MoveItem(item, LocalPlayerData(), ownOffer, 0, true, false);
        strip->ScrollToItem(held);
        net::g_gameClient->PublishParty();
        return;
    }
}
