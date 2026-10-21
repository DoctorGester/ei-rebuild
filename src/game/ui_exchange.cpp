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
#include "originals_gameplay.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"
#include "world_glue.h"
#include "game_data.h"

#include <cstdint>

namespace {
const char* const kBuySound = "buttons\\camp\\buy.wav";
const char* const kCancelSound = "buttons\\messbox\\cancel.wav";
const char* const kTransitSound = "buttons\\globalmap\\transit.wav";
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
    static_cast<CItemInfoPanel*>(descPanel_)->Describe(nullptr);
    delete scratch_;   // slot 0 with 1 (the game's deleting destructor)
}
EI_REPLACE_METHOD(0x0062C8F0, CExchangeScreen::Close);

void CExchangeScreen::OnMessageResult(int) {}
EI_REPLACE_METHOD(0x0062C920, CExchangeScreen::OnMessageResult);

// --- 0062C930: the update (slot 24) ----------------------------------------------------------------

void CExchangeScreen::Update() {
    UiUpdateChildrenOriginal(this);
    if (!LiveOffer()) {
        ReturnAll(LocalParty(), ownOffer_);
        GameRejoin();
        return;
    }
    bool changed = moneyField_->changed_;
    moneyField_->changed_ = false;
    if (!changed) return;
    ownOffer_->Money() = libcrt::Atoi(moneyField_->text_);
    net::ClientPublishParty(net::GameClient());
}
EI_REPLACE_METHOD(0x0062C930, CExchangeScreen::Update);

// --- 0062CF70: the left button (input slot 2) --------------------------------------------------------

void CExchangeScreen::OnLButtonDown(int buttons, int x, int y) {
    InputOf(this)->OnLeftDown(buttons, x, y);
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
namespace {
} // namespace
EI_REPLACE_METHOD(0x0062CF70, InputForward<&CExchangeScreen::OnLButtonDown>::Call);

// --- 0062D210: the exchange went through (slot 37) -------------------------------------------------

void CExchangeScreen::OnExchangeDone(CParty* partyB, CParty* partyA, CParty* partyC) {
    PartyReleaseItemsOriginal(LocalPlayerData());
    auto* local = reinterpret_cast<CParty*>(LocalPlayerData());
    ReturnAll(local, partyC);
    ReturnAll(reinterpret_cast<CParty*>(LocalPlayerData()), partyB);
    int money = partyB->Money() - partyA->Money() + partyC->Money();
    reinterpret_cast<CParty*>(LocalPlayerData())->Money() = money;
    GameRejoin();
}
EI_REPLACE_METHOD(0x0062D210, CExchangeScreen::OnExchangeDone);

// --- 0062C250: Create (slot 25) --------------------------------------------------------------------

namespace {

void** Vtable(std::uintptr_t address) { return ei::GamePointer<void*>(address); }

VtablePair ItemPanelVtables() { return {Vtable(0x00741B58), Vtable(0x00741B24)}; }          // CUiItemPanel
VtablePair InfoPanelVtables() { return {Vtable(0x007404DC), Vtable(0x007404A8)}; }          // CItemInfoPanel
VtablePair ForwardingStripVtables() { return {Vtable(0x0074197C), Vtable(0x00741948)}; }
VtablePair FigureStripVtables() { return {Vtable(0x00741BF4), Vtable(0x00741C60)}; }
VtablePair TradeGridVtables() { return {Vtable(0x0074170C), Vtable(0x007416D8)}; }
std::uintptr_t ItemArrayVtable() { return ei::DataId(0x00741BC0); }   // the strip's CArray<char*>
constexpr std::size_t kFigureStripSize = 0x198;   // the figure strip stops before the item list
constexpr int kSurfaceFormat = 5;
constexpr int kStripActionTake = 1;
constexpr int kGridMode = 5;

void SetVtables(CUiWindow* window, VtablePair vtables) {
    window->vtable_ = vtables.main;
    window->inputVtable_ = vtables.input;
}

// A window of `size` bytes built on the window base with the vtable pair, or null.
template <typename T>
T* NewChild(std::size_t size, CInterface3D* ui, VtablePair vtables) {
    void* memory = gamecrt::operator_new(size);
    if (!memory) return nullptr;
    auto* window = static_cast<CUiWindow*>(memory);
    window->Construct(ui);
    SetVtables(window, vtables);
    return static_cast<T*>(memory);
}

CUiSurface* NewSurface(CInterface3D* ui, int top, int bottom) {
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    return memory ? static_cast<CUiSurface*>(memory)->Construct(ui, 0, top, 200, bottom, kSurfaceFormat, 1) : nullptr;
}

CUiContainer* AsContainer(CExchangeScreen* screen) { return reinterpret_cast<CUiContainer*>(screen); }

// 0041E220 (net::ClientFindParty): the partner's offer that pairs with ours, or null.
net::PartyMessage* ClientFindPartyAt(net::ClientSession* client) {
    return static_cast<net::PartyMessage*>(net::ClientFindParty(client));
}
} // namespace

void CExchangeScreen::Create() {
    LoadTexture("campinfo", 3, nullptr);
    CUiRegion* region = AddRegion();
    region->CallSetRect(12, 450, 52, 490);
    region->SetTip(0xA028, 1, 0);
    region = AddRegion();
    region->CallSetRect(148, 450, 188, 490);
    region->SetTip(0xA029, 1, 0);
    region = AddRegion();
    region->CallSetRect(740, 460, 780, 490);
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
    static_cast<CItemIconWindow*>(iconWindow_)->PlaceBars(300);

    // The scratch party (made through its slot 2, not checked), our offer cleared, the partner's.
    void* memory = gamecrt::operator_new(sizeof(CParty));
    scratch_ = memory ? static_cast<CParty*>(static_cast<CParty*>(memory)->Construct()) : nullptr;
    scratch_->Create();
    net::ClientSession* client = net::GameClient();
    ownOffer_ = reinterpret_cast<CParty*>(&client->parties_[0]);
    partnerOffer_ = reinterpret_cast<CParty*>(&client->parties_[1]);
    PartyResetOriginal(ownOffer_, nullptr, 1, 0);

    // The description panel (its item list and text arrays constructed in place).
    auto* panel = NewChild<CItemInfoPanel>(sizeof(CItemInfoPanel), interface_, ItemPanelVtables());
    if (panel) {
        panel->owner_ = this;
        panel->ItemParty()->Construct();
        panel->itemTexts_.Construct();
        panel->skillTexts_.Construct();
        SetVtables(panel, InfoPanelVtables());
    }
    descPanel_ = panel;
    AsContainer(this)->AddChild(panel);
    descPanel_->Build(600, 100);

    // The local party's items (the strip that hands clicks to this screen).
    memory = gamecrt::operator_new(sizeof(CUiItemStrip));
    CUiItemStrip* strip = nullptr;
    if (memory) {
        strip = static_cast<CUiItemStrip*>(memory);
        strip->window.Construct(interface_);
        strip->owner_ = this;
        strip->list_.Construct(ItemArrayVtable());
        SetVtables(&strip->window, ForwardingStripVtables());
    }
    strip_ = strip;
    AsContainer(this)->AddChild(strip ? &strip->window : nullptr);
    strip_->Setup(reinterpret_cast<UiPtrArray<char>*>(&LocalParty()->items_), 0);
    strip_->actionMode_ = kStripActionTake;
    strip_->SetFilter(0);

    // The players.
    figures_ = NewChild<CUiItemStrip>(kFigureStripSize, interface_, FigureStripVtables());
    if (figures_) figures_->owner_ = this;
    AsContainer(this)->AddChild(figures_ ? &figures_->window : nullptr);
    figures_->SetupFigures(&names_, 1, 1);

    // Both offers.
    grid_ = NewChild<CTradeGrid>(sizeof(CTradeGrid), interface_, TradeGridVtables());
    if (grid_) grid_->owner_ = this;
    AsContainer(this)->AddChild(grid_);
    grid_->Build(&partnerOffer_->items_, &ownOffer_->items_, 0, 1);
    grid_->mode_ = kGridMode;

    // The money offered.
    memory = gamecrt::operator_new(sizeof(CUiEditField));
    moneyField_ = memory ? static_cast<CUiEditField*>(memory)->Construct(interface_, RECT{0, 0x15E, 200, 0x16E}) : nullptr;
    AsContainer(this)->AddChild(moneyField_);
    moneyField_->SetLook(1, static_cast<char>(1), 1, 9, 0xFFFFFF, 0x31B3FF);
    static_cast<CUiEditField*>(moneyField_)->SetText("");

    AddSurface(NewSurface(interface_, 0x19A, 0x1C2));
    AddSurface(NewSurface(interface_, 0x136, 0x15E));
    AddSurface(NewSurface(interface_, 0xD2, 0x10E));
    AddSurface(NewSurface(interface_, 0x6E, 0xAA));
}
EI_REPLACE_METHOD(0x0062C250, CExchangeScreen::Create);

// --- 0062C9C0: Draw (slot 20) ----------------------------------------------------------------------

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
CUiMesh* Sprite(CEngineObject* object) { return reinterpret_cast<CUiMesh*>(object); }
bool LiveAccepted(const net::PartyMessage* offer) {
    return offer && !(offer->flags7_ & CRefObject::kDetached) && offer->accepted_;
}
} // namespace

BOOL CExchangeScreen::Draw(int pass) {
    // What is under the mouse: an item of the strip or else of the grid, or a help topic.
    hoveredItem_ = nullptr;
    int topic = 0;
    hoveredItem_ = reinterpret_cast<CItem*>(strip_->window.VirtualItemAt(MouseScreenX(), MouseScreenY(), &topic));
    if (!hoveredItem_ && !topic)
        hoveredItem_ = reinterpret_cast<CItem*>(grid_->VirtualItemAt(MouseScreenX(), MouseScreenY(), &topic));
    if (hoveredItem_) {
        if (descPanel_) static_cast<CItemInfoPanel*>(descPanel_)->Describe(hoveredItem_);
    } else if (topic) {
        if (descPanel_) static_cast<CItemInfoPanel*>(descPanel_)->ShowHelp(topic);
    } else if (descPanel_) {
        static_cast<CItemInfoPanel*>(descPanel_)->Describe(nullptr);
    }

    auto* texts = reinterpret_cast<CTextManager*>(GameClientTexts());
    mfc::CString text, line;
    net::ClientSession* client = net::GameClient();

    // Surface 0: our money and the accept caption.
    CUiSurface* surface = surfaces_.data[0];
    UiSurfaceGetDC(surface);
    surface->Clear();
    texts->GetString("xch_current_money", &text);
    line.Format("%s %d", static_cast<const char*>(text), LocalParty()->Money());
    RECT rect{0, 0, 200, 0x14};
    SurfaceDrawText(surface, nullptr, line, &rect, kAlignLeft, kWhite, 1, 1);
    texts->GetString("xch_accept", &text);
    rect = RECT{0, 0x14, 200, 0x28};
    SurfaceDrawText(surface, nullptr, text, &rect, kAlignLeft, kWhite, 1, 1);
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
    net::PartyMessage* partner = ClientFindPartyAt(client);
    if (partner && !(partner->flags7_ & CRefObject::kDetached)) {
        texts->GetString("xch_money_remote", &text);
        rect = RECT{0x14, 0, 0xB4, 0x14};
        surface->DrawWrappedText(text, &rect, kWrapLines, 1, kWhite, 1, 1);
        line.Format("%d", partnerOffer_->Money());
        rect = RECT{0, 0x28, 200, 0x3C};
        SurfaceDrawText(surface, nullptr, line, &rect, kAlignLeft, kWhite, 1, 1);
    }
    UiSurfaceReleaseDC(surface);

    // Surface 3: whether the partner accepted.
    surface = surfaces_.data[3];
    UiSurfaceGetDC(surface);
    surface->Clear();
    partner = ClientFindPartyAt(client);
    if (partner && !(partner->flags7_ & CRefObject::kDetached)) {
        texts->GetString("xch_remote_header", &text);
        rect = RECT{0x14, 0, 0xB4, 0x14};
        surface->DrawWrappedText(text, &rect, kWrapLines, 1, kWhite, 1, 1);
        texts->GetString(ClientFindPartyAt(client)->accepted_ ? "xch_remote_ok"
                                                              : "xch_remote_cancel",
                         &text);
        rect = RECT{0, 0x28, 200, 0x3C};
        SurfaceDrawText(surface, nullptr, text, &rect, kAlignLeft, kGrey, 1, 1);
    }
    UiSurfaceReleaseDC(surface);

    // The accept button greyed and take-back lit while our offer is accepted, else the reverse;
    // the third one lit while the partner accepted.
    bool accepted = LiveAccepted(static_cast<net::PartyMessage*>(client->party_.ptr));
    SetTint(Sprite(sceneObjects_.data[0]), accepted ? 0.5f : 1.0f);
    SetTint(Sprite(sceneObjects_.data[1]), accepted ? 1.0f : 0.5f);
    partner = ClientFindPartyAt(client);
    bool partnerAccepted = partner && !(partner->flags7_ & CRefObject::kDetached) && ClientFindPartyAt(client)->accepted_;
    SetTint(Sprite(sceneObjects_.data[2]), partnerAccepted ? 0.5f : 1.0f);

    AsContainer(this)->DrawContainer(pass);
    return 1;
}
EI_REPLACE_METHOD(0x0062C9C0, CExchangeScreen::Draw);

// --- 0062D100 / 0062D200: an item clicked in the strip or the grid (slots 42 / 43) -------------------

namespace {
const char* const kPutOnSound = "buttons\\camp\\put_on.wav";
const char* const kPutOffSound = "buttons\\camp\\put_off.wav";
constexpr int kSlotCanMove = 43;
constexpr int kOfferLimit = 8;
} // namespace

bool CExchangeScreen::CanMove(CItem*, int) { return true; }
EI_REPLACE_METHOD(0x0062D200, CExchangeScreen::CanMove);

void CExchangeScreen::MoveOffered(CItem* item, int mode) {
    using CanMoveFn = bool(__thiscall*)(CExchangeScreen*, CItem*, int);
    if (!reinterpret_cast<CanMoveFn>(vtable_[kSlotCanMove])(this, item, mode)) {
        uiorig::PlayUiSound(interface_, kCancelSound, 0);
        return;
    }
    if (mode == kStripActionTake) {
        uiorig::PlayUiSound(interface_, kPutOnSound, 0);
        CParty* local = LocalParty();
        static_cast<CItemScreen*>(this)->MoveItem(item, static_cast<CParty*>(ownOffer_), local, kOfferLimit, 1, 0);
        net::ClientPublishParty(net::GameClient());
        return;
    }
    if (mode != kGridMode) return;
    const AlignedPtrArray& offered = ownOffer_->items_;
    for (int i = 0; i < offered.size; ++i) {
        if (offered.data[i] != item) continue;
        uiorig::PlayUiSound(interface_, kPutOffSound, 0);
        CParty* offer = ownOffer_;
        CItem* held = static_cast<CItemScreen*>(this)->MoveItem(item, LocalParty(), offer, 0, 1, 0);
        static_cast<CUiItemStrip*>(strip_)->ScrollToItem(held);
        net::ClientPublishParty(net::GameClient());
        return;
    }
}
EI_REPLACE_METHOD(0x0062D100, CExchangeScreen::MoveOffered);
