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
#include <cstring>
#include <initializer_list>
#include <new>

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

// --- One-line setters and readers ------------------------------------------------------------------------

void CUiMesh::SetScaleYBits(std::uint32_t bits) { scaleMatrix_.m[5] = std::bit_cast<float>(bits); }


// --- Member constructors and destructors -----------------------------------------------------------------

// The interface manager.
CInterface3D g_interface;

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

void CEscMenu::CloseScreen() { models_[0]->Play(0); }

void CMessageBox::CloseScreen() {
    ClearSceneObjects();
    ClearSurfaces();
}
