// Small members of the interface classes: member constructors and assignments, default virtual answers,
// one-line setters and handlers. See ui.h, ui_camp.h, ui_screens.h and docs/notes/ui.md.
#include "console.h"
#include "ui.h"
#include "ui_briefing.h"
#include "ui_camp.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include <bit>
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

// --- Default answers of the windows' slots ---------------------------------------------------------------

void UiInputSide::Wheel(int, int, int, int) {}

CItem* CUiItemPanel::ItemAt(int, int, int* topic) {
    *topic = 0;
    return nullptr;
}

bool CItemScreen::CanEquip(CItem*, int) { return false; }

PartySkill CItemScreen::BestPartySkill(int) { return {}; }

CUnit* CItemScreen::HeroUnit(int) { return nullptr; }

double CItemScreen::Weight() { return 0.0; }

int CItemScreen::Price(CItem*, int) { return 0; }

CItem* CFigureStrip::ItemAt(int, int, int*) { return nullptr; }

const char* CGlobalMap::HelpTopic() { return noCamp_ == 1 ? "global_map_nocamp" : "global_map"; }

// --- One-line setters and readers ------------------------------------------------------------------------


void CBriefingScreen::SetChatActive(int mode) { chatActive_ = mode; }

void CUiItemStrip::SetSlots(int slots) { slots_ = slots; }

CItem* CItemInfoPanel::FirstItem() { return std::ssize(itemParty_->items_) ? itemParty_->items_[0] : nullptr; }

void CUiMesh::SetScaleYBits(std::uint32_t bits) { scaleMatrix_.m[5] = std::bit_cast<float>(bits); }


// --- Member constructors and destructors -----------------------------------------------------------------

CScreenFade::CScreenFade() {
    alpha_ = 0.0f;
    active_ = 0;
    letterbox_ = 0;
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

void ItemTurn::OnShutdown() {}

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

void CHeroStatsPanel::LeftUp(int, int, int) {
    interface_->mouseCapture_ = nullptr;
    heldRegion_ = -1;
    holdTime_ = 0.0f;
}

void CGlobalMap::LeftUp(int, int, int) {
    if (turning_ != 0) selectPending_ = 1;
    turning_ = 0;
}

void CEscMenu::CloseScreen() { models_[0]->Play(0); }

void CMessageBox::CloseScreen() {
    ClearSceneObjects();
    ClearSurfaces();
}
