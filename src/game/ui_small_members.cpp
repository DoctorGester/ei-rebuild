// Small members of the interface classes: member constructors and assignments, default virtual answers,
// one-line setters and handlers. See ui.h, ui_camp.h, ui_screens.h and docs/notes/ui.md.
#include "console.h"
#include "map_object_figures.h"
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

Item* UiItemPanel::ItemAt(int, int, int* topic) {
    *topic = 0;
    return nullptr;
}

bool ItemScreen::CanEquip(Item*, int) { return false; }

PartySkill ItemScreen::BestPartySkill(int) { return {}; }

CUnit* ItemScreen::HeroUnit(int) { return nullptr; }

double ItemScreen::Weight() { return 0.0; }

int ItemScreen::Price(Item*, int) { return 0; }

Item* FigureStrip::ItemAt(int, int, int*) { return nullptr; }

// --- One-line setters and readers ------------------------------------------------------------------------

void UiMesh::SetScaleYBits(std::uint32_t bits) { scaleMatrix.m[5] = std::bit_cast<float>(bits); }


// --- Member constructors and destructors -----------------------------------------------------------------

// The interface manager.
Interface3D g_interface;

void ItemTurn::OnShutdown() {}

// --- Handlers ----------------------------------------------------------------------------------------------

void console::ConsoleWindow::Blit() {
    if (surface) surface->Blit();
}

void Interface3D::ChatKeyEnter() {
    if (!root->chatOpen) root->SetOpen(true);
}

void Interface3D::ChatKeyBackspace() {
    if (!root->chatOpen) root->ClearLines();
}

void HeroStatsPanel::LeftUp(int, int, int) {
    ui->mouseCapture = nullptr;
    heldRegion = -1;
    holdTime = 0.0f;
}

void EscMenu::CloseScreen() { models[0]->Play(SkillAnimation::Briefing01); }

void MessageWindow::CloseScreen() {
    ClearSceneObjects();
    ClearSurfaces();
}
