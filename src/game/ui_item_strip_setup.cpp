// The item strips' setups: the camp's item, spell and part strips and the
// hero screen's figure strips, called by their screens' Creates after constructing the strip;
// and the figure strips' draw, count and click.
// See ui_screens.h (UiItemStrip::Setup, SetupFigures) and docs/notes/ui.md ("Camp, global map,
// hero screens").
#include "ui.h"
#include "ui_item_info.h"
#include "ui_screens.h"

#include <cstdint>
#include <string>

#include <bit>

namespace {

constexpr const char* kTopTexture = "Inventory02";
constexpr const char* kBottomTexture = "Inventory01";
constexpr const char* kFigureTexture = "InventoryM01";


// Layout constants of the strips (the exe's floats).
constexpr float kCell = 100.0f;
constexpr float kHalfCell = 50.0f;
constexpr float kFrameDepth = 0.9f;
constexpr float kFaceDepth = 1.5f;
constexpr float kSlotsPerPixel = 0.01f;
constexpr float kDimmed = 0.1953125f;   // 0.1953125 as the end faces' texture offset

const char* const kOnOffSound = "buttons\\battle\\on_off.wav";

} // namespace

void UiItemStrip::Setup(std::vector<Item*>* newSource, bool newTop) {
    LoadTexture(newTop ? kTopTexture : kBottomTexture, TextureKind::Interface);
    Build(newTop);
    regions[0]->SetTip(0x4FB0);
    regions[1]->SetTip(0x4FB1);
    AddSurface(new UiSurface(ui, 50, top + 15, 750, top + 35, SurfaceBlend::Keyed, true));
    AddSurface(new UiSurface(ui, 50, top + 65, 750, top + 85, SurfaceBlend::Keyed, true));
    slots = 9;
    source = newSource;
}

void FigureStrip::SetupFigures(std::vector<std::string>* names, bool newTop, bool noArrowTips) {
    figures = names;
    LoadTexture(kFigureTexture, TextureKind::Interface);
    Build(newTop);
    if (!noArrowTips) {
        regions[0]->SetTip(0x9DD0);
        regions[1]->SetTip(0x9DD1);
    }
    tabsEnabled = false;
    for (int i = 2; i < 8; ++i) SceneMesh(i)->visible = false;
}

// --- The figure strips' slots ----------------------------------------------------------------------

void FigureStrip::Draw(SceneRenderer* pass) {
    const float scrollPosition = position[tab];
    const int first = static_cast<int>(scrollPosition);   // _ftol
    const float frac = static_cast<float>(scrollPosition - static_cast<float>(first));
    SceneMesh(0)->textureOffsetU = position[tab] > 0.0f ? kDimmed : 0.0f;
    SceneMesh(1)->textureOffsetU = static_cast<float>(count - 7) > position[tab] ? kDimmed : 0.0f;
    SceneMesh(9)->Place(kHalfCell - frac * kCell, static_cast<float>(top), scale + scale);
    for (int i = 0; i < 8; ++i) {
        SceneMesh(10 + i)->Place(((static_cast<float>(i) - frac) + 1.0f) * kCell, static_cast<float>(top) + kHalfCell,
                                 scale);
    }
    UiMesh* frame = SceneMesh(8);
    frame->visible = false;
    if (selected >= 0 && selected < count) {
        int cell = selected - first;
        if (cell >= 0 && cell < 8) {
            frame->visible = true;
            frame->Place(((static_cast<float>(cell) - frac) + 1.0f) * kCell, static_cast<float>(top) + kHalfCell,
                         scale * kFrameDepth);
        }
    }
    DrawBase(pass);
    for (int i = 0; i < 8; ++i) {
        int index = first + i;
        if (index < 0 || index >= count) continue;
        const HeroFaceCell cell{(*figures)[index].c_str(), ((static_cast<float>(i) - frac) + 1.0f) * kCell,
                                static_cast<float>(top) + kHalfCell, scale * kFaceDepth, kCell};
        owner->DrawHeroFace(pass, cell);
    }
}

void FigureStrip::RebuildList() { count = static_cast<int>(figures->size()); }

void FigureStrip::LeftDown(int buttons, int x, int y) {
    UiItemStrip::LeftDown(buttons, x, y);
    if (RegionAt(x, y) != 8) return;
    POINT point{x, y};
    ui->ScalePointFromScreenEntry(&point);
    int index = static_cast<int>(static_cast<double>(point.x - 50) * kSlotsPerPixel + position[tab]);     // _ftol
    if (index < 0 || index >= count) return;
    ui->PlaySound(kOnOffSound);
    owner->ChooseFigure((*figures)[index]);     // the hero screen: the new hero's figure
}
