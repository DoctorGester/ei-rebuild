// Small members of the interface classes: member constructors and assignments, default virtual answers,
// one-line setters and handlers. See ui.h, ui_camp.h, ui_screens.h and docs/notes/ui.md.
#include "console.h"
#include "game_strings.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>

namespace {
} // namespace

// --- The battle screen's members constructed by CreateScreen 1 ---------------------------------------

CBattleScreen::PartyIds* CBattleScreen::PartyIds::Construct() {
    for (IdVector* ids : {&withUnit, &without}) {
        ids->Start() = nullptr;
        ids->Finish() = nullptr;
        ids->EndOfStorage() = nullptr;
    }
    return this;
}
EI_REWRITTEN(0x005F6080, CBattleScreen::PartyIds::Construct);

LockKey* LockKey::ConstructItemUseKey() {
    mask = 1;
    id = 0;
    level = 0;
    return this;
}
EI_REWRITTEN(0x005F5F90, LockKey::ConstructItemUseKey);

// --- A global map place assigned ----------------------------------------------------------------------

GlobalMapPlace* GlobalMapPlace::Assign(const GlobalMapPlace& from) {
    zone = from.zone;
    position = from.position;
    name = from.name;
    title = from.title;
    return this;
}
EI_REWRITTEN(0x00632490, GlobalMapPlace::Assign);

GlobalMapPlace* GlobalMapPlace::AssignCopy(const GlobalMapPlace& from) { return Assign(from); }
EI_REWRITTEN(0x006324E0, GlobalMapPlace::AssignCopy);

// --- Default answers of the windows' slots ---------------------------------------------------------------

int UiInputSide::Wheel(int, int, int, int) { return 0; }
EI_REWRITTEN(0x005E28B0, UiInputSide::Wheel);

CItem* CUiItemPanel::ItemAt(int, int, int* topic) {
    *topic = 0;
    return nullptr;
}
EI_REWRITTEN(0x005EE400, CUiItemPanel::ItemAt);

bool CItemScreen::CanEquip(CItem*, int) { return false; }
EI_REWRITTEN(0x005F6380, CItemScreen::CanEquip);

void CItemScreen::BestPartySkill(int, int* skill, int* mana) {
    *skill = 0;
    *mana = 0;
}
EI_REWRITTEN(0x005F63A0, CItemScreen::BestPartySkill);

CUnit* CItemScreen::HeroUnit(int) { return nullptr; }
EI_REWRITTEN(0x005F6400, CItemScreen::HeroUnit);

double CItemScreen::Weight() { return 0.0 /*0073B858*/; }
EI_REWRITTEN(0x005F6430, CItemScreen::Weight);

int CItemScreen::Price(CItem*, int) { return 0; }
EI_REWRITTEN(0x005F6440, CItemScreen::Price);

CItem* CFigureStrip::ItemAt(int, int, int*) { return nullptr; }
EI_REWRITTEN(0x006157C0, CFigureStrip::ItemAt);

bool CGlobalMap::MayTravel(int) { return true; }
EI_REWRITTEN(0x006316A0, CGlobalMap::MayTravel);

const char* CGlobalMap::HelpTopic() { return noCamp_ == 1 ? "global_map_nocamp" : "global_map"; }
EI_REWRITTEN(0x0062EE70, CGlobalMap::HelpTopic);

// --- One-line setters and readers ------------------------------------------------------------------------

void CBattleScreen::SetPortraitMode(int mode) { portraitMode_ = mode; }
EI_REWRITTEN(0x005F5FB0, CBattleScreen::SetPortraitMode);

void CBriefingScreen::SetChatActive(int mode) { chatActive_ = mode; }
EI_REWRITTEN(0x005F6B70, CBriefingScreen::SetChatActive);

CUnit* CCampPages::HeroAt(int index) { return heroes_[index]; }
EI_REWRITTEN(0x005F65F0, CCampPages::HeroAt);

void CUiItemStrip::SetSlots(int slots) { slots_ = slots; }
EI_REWRITTEN(0x00621F20, CUiItemStrip::SetSlots);

std::uint8_t CTradePanel::Bought() const { return records_[recordIndex_].bought; }
EI_REWRITTEN(0x00625510, CTradePanel::Bought);

CItem* CItemInfoPanel::FirstItem() { return itemParty_->items_.size ? itemParty_->items_.data[0] : nullptr; }
EI_REWRITTEN(0x005EE720, CItemInfoPanel::FirstItem);

void CUiMesh::SetScaleYBits(std::uint32_t bits) { std::memcpy(&scaleMatrix_.m[5], &bits, sizeof(bits)); }
EI_REWRITTEN(0x005E7090, CUiMesh::SetScaleYBits);

float CUiMesh::GetScaleX() const { return scaleMatrix_.m[0]; }
EI_REWRITTEN(0x005E70B0, CUiMesh::GetScaleX);

// --- Member constructors and destructors -----------------------------------------------------------------

CScreenFade* CScreenFade::Construct() {
    alpha_ = 0.0f;
    active_ = 0;
    letterbox_ = 0;
    return this;
}
EI_REWRITTEN(0x005E6990, CScreenFade::Construct);

CInterface3D* CInterface3D::Construct() {
    stack_.Start() = nullptr;
    stack_.Finish() = nullptr;
    stack_.EndOfStorage() = nullptr;
    messages_.Construct();
    helpTopics_.Construct();
    std::construct_at(&messageText_);
    std::construct_at(&savedText_);
    return this;
}
EI_REWRITTEN(0x005F46A0, CInterface3D::Construct);

// The interface manager, zero at the start: built by a static initialiser in the list's order and destroyed
// by the exit function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CInterface3D> g_interface = /*007C7D50*/ {};
CInterface3D* TheInterface() { return &*g_interface; }

namespace {
void __cdecl DestroyInterface() { g_interface->DestroyBody(); }
} // namespace

void __cdecl ConstructInterface() {
    g_interface->Construct();
    libcrt::AtExit(DestroyInterface);
}

EI_REWRITTEN(0x005F4680, ConstructInterface);
EI_REWRITTEN(0x005F4780, DestroyInterface);

void CUiMesh::DestroySprite() {
    material_ = nullptr;
    texture_ = nullptr;
    CPlainFigureMesh::Destroy();
}
EI_REWRITTEN(0x005E6D70, CUiMesh::DestroySprite);

int ItemTurn::OnShutdown() { return 0; }
EI_REWRITTEN(0x005ED760, ItemTurn::OnShutdown);

// --- Handlers ----------------------------------------------------------------------------------------------

void console::ConsoleWindow::Blit() {
    if (surface_) surface_->Blit();
}
EI_REWRITTEN(0x005E3FC0, console::ConsoleWindow::Blit);

void CInterface3D::ChatKeyEnter() {
    if (!root_->chatOpen_) root_->SetOpen(true);
}
EI_REWRITTEN(0x005F7C60, CInterface3D::ChatKeyEnter);

void CInterface3D::ChatKeyBackspace() {
    if (!root_->chatOpen_) root_->ClearLines();
}
EI_REWRITTEN(0x005F7C80, CInterface3D::ChatKeyBackspace);

void CCampPages::OnKeyUp(int key, int repeat, int flags) { CUiContainer::KeyUp(key, repeat, flags); }
EI_REWRITTEN(0x0060F690, CCampPages::OnKeyUp);

int CHeroStatsPanel::OnLButtonUp(int, int, int) {
    interface_->mouseCapture_ = nullptr;
    heldRegion_ = -1;
    holdTime_ = 0.0f;
    return 0;
}
EI_REWRITTEN(0x00624A40, CHeroStatsPanel::OnLButtonUp);

int CGlobalMap::OnLButtonUp(int, int, int) {
    if (turning_ != 0) selectPending_ = 1;
    turning_ = 0;
    return 0;
}
EI_REWRITTEN(0x0062FEF0, CGlobalMap::OnLButtonUp);

void CEscMenu::OnClose() { models_.data[0]->Play(0); }
EI_REWRITTEN(0x0062ACB0, CEscMenu::OnClose);

void CMessageBox::OnClose() {
    ClearSceneObjects();
    ClearSurfaces();
}
EI_REWRITTEN(0x00634D00, CMessageBox::OnClose);
