// Small members of the interface classes: member constructors and assignments, default virtual answers,
// one-line setters and handlers. See ui.h, ui_camp.h, ui_screens.h and docs/notes/ui.md.
#include "console.h"
#include "game_strings.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include "lib/mfc.h"
#include "game_data.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>

// --- The battle screen's members constructed by CreateScreen 1 ---------------------------------------

LockKey* LockKey::ConstructItemUseKey() {
    mask = 1;
    id = 0;
    level = 0;
    return this;
}

// --- A global map place assigned ----------------------------------------------------------------------

GlobalMapPlace* GlobalMapPlace::Assign(const GlobalMapPlace& from) {
    zone = from.zone;
    position = from.position;
    name = from.name;
    title = from.title;
    return this;
}

GlobalMapPlace* GlobalMapPlace::AssignCopy(const GlobalMapPlace& from) { return Assign(from); }

// --- Default answers of the windows' slots ---------------------------------------------------------------

int UiInputSide::Wheel(int, int, int, int) { return 0; }

CItem* CUiItemPanel::ItemAt(int, int, int* topic) {
    *topic = 0;
    return nullptr;
}

bool CItemScreen::CanEquip(CItem*, int) { return false; }

void CItemScreen::BestPartySkill(int, int* skill, int* mana) {
    *skill = 0;
    *mana = 0;
}

CUnit* CItemScreen::HeroUnit(int) { return nullptr; }

double CItemScreen::Weight() { return 0.0 /*0073B858*/; }

int CItemScreen::Price(CItem*, int) { return 0; }

CItem* CFigureStrip::ItemAt(int, int, int*) { return nullptr; }

bool CGlobalMap::MayTravel(int) { return true; }

const char* CGlobalMap::HelpTopic() { return noCamp_ == 1 ? "global_map_nocamp" : "global_map"; }

// --- One-line setters and readers ------------------------------------------------------------------------

void CBattleScreen::SetPortraitMode(int mode) { portraitMode_ = mode; }

void CBriefingScreen::SetChatActive(int mode) { chatActive_ = mode; }

CUnit* CCampPages::HeroAt(int index) { return heroes_[index]; }

void CUiItemStrip::SetSlots(int slots) { slots_ = slots; }

std::uint8_t CTradePanel::Bought() const { return records_[recordIndex_].bought; }

CItem* CItemInfoPanel::FirstItem() { return itemParty_->items_.size ? itemParty_->items_.data[0] : nullptr; }

void CUiMesh::SetScaleYBits(std::uint32_t bits) { std::memcpy(&scaleMatrix_.m[5], &bits, sizeof(bits)); }

float CUiMesh::GetScaleX() const { return scaleMatrix_.m[0]; }

// --- Member constructors and destructors -----------------------------------------------------------------

CScreenFade* CScreenFade::Construct() {
    alpha_ = 0.0f;
    active_ = 0;
    letterbox_ = 0;
    return this;
}

CInterface3D* CInterface3D::Construct() {
    std::construct_at(&stack_);
    std::construct_at(&messages_);
    std::construct_at(&helpTopics_);
    std::construct_at(&messageText_);
    std::construct_at(&savedText_);
    return this;
}

// The interface manager, zero at the start: built by a static initialiser in the list's order and destroyed
// by the exit function it registers (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CInterface3D> g_interface = /*007C7D50*/ {};
CInterface3D* TheInterface() { return &*g_interface; }

namespace {
void DestroyInterface() { g_interface->DestroyBody(); }
} // namespace

void ConstructInterface() {
    g_interface->Construct();
    std::atexit(DestroyInterface);
}

void CUiMesh::DestroySprite() {
    material_ = nullptr;
    texture_ = nullptr;
    CPlainFigureMesh::Destroy();
}

int ItemTurn::OnShutdown() { return 0; }

// --- Handlers ----------------------------------------------------------------------------------------------

void console::ConsoleWindow::Blit() {
    if (surface_) surface_->Blit();
}

void CInterface3D::ChatKeyEnter() {
    if (!root_->chatOpen_) root_->SetOpen(true);
}

void CInterface3D::ChatKeyBackspace() {
    if (!root_->chatOpen_) root_->ClearLines();
}

void CCampPages::OnKeyUp(int key, int repeat, int flags) { CUiContainer::KeyUp(key, repeat, flags); }

int CHeroStatsPanel::OnLButtonUp(int, int, int) {
    interface_->mouseCapture_ = nullptr;
    heldRegion_ = -1;
    holdTime_ = 0.0f;
    return 0;
}

int CGlobalMap::OnLButtonUp(int, int, int) {
    if (turning_ != 0) selectPending_ = 1;
    turning_ = 0;
    return 0;
}

void CEscMenu::OnClose() { models_.data[0]->Play(0); }

void CMessageBox::OnClose() {
    ClearSceneObjects();
    ClearSurfaces();
}
