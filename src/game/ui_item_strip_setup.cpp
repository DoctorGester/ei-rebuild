// The item strips' setups: the camp's item, spell and part strips and the
// hero screen's figure strips, called by their screens' Creates after constructing the strip;
// and the figure strips' draw, count and click.
// See ui_screens.h (CUiItemStrip::Setup, SetupFigures) and docs/notes/ui.md ("Camp, global map,
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

void CUiItemStrip::Setup(std::vector<CItem*>* source, bool top) {
    LoadTexture(top ? kTopTexture : kBottomTexture, TextureKind::Interface);
    Build(top);
    regions_[0]->SetTip(0x4FB0);
    regions_[1]->SetTip(0x4FB1);
    AddSurface(new CUiSurface(interface_, 50, top_ + 15, 750, top_ + 35, SurfaceBlend::Keyed, true));
    AddSurface(new CUiSurface(interface_, 50, top_ + 65, 750, top_ + 85, SurfaceBlend::Keyed, true));
    slots_ = 9;
    source_ = source;
}

void CFigureStrip::SetupFigures(std::vector<std::string>* names, bool top, bool noArrowTips) {
    figures_ = names;
    LoadTexture(kFigureTexture, TextureKind::Interface);
    Build(top);
    if (!noArrowTips) {
        regions_[0]->SetTip(0x9DD0);
        regions_[1]->SetTip(0x9DD1);
    }
    tabsEnabled_ = false;
    for (int i = 2; i < 8; ++i) SceneMesh(i)->visible_ = false;
}

// --- The figure strips' slots ----------------------------------------------------------------------

void CFigureStrip::Draw(CSceneRenderer* pass) {
    const float position = position_[tab_];
    const int first = static_cast<int>(position);   // _ftol
    const float frac = static_cast<float>(position - static_cast<float>(first));
    SceneMesh(0)->textureOffsetU_ = position_[tab_] > 0.0f ? kDimmed : 0.0f;
    SceneMesh(1)->textureOffsetU_ = static_cast<float>(count_ - 7) > position_[tab_] ? kDimmed : 0.0f;
    SceneMesh(9)->Place(kHalfCell - frac * kCell, static_cast<float>(top_), scale_ + scale_);
    for (int i = 0; i < 8; ++i) {
        SceneMesh(10 + i)->Place(((static_cast<float>(i) - frac) + 1.0f) * kCell, static_cast<float>(top_) + kHalfCell,
                                 scale_);
    }
    CUiMesh* frame = SceneMesh(8);
    frame->visible_ = false;
    if (selected_ >= 0 && selected_ < count_) {
        int cell = selected_ - first;
        if (cell >= 0 && cell < 8) {
            frame->visible_ = true;
            frame->Place(((static_cast<float>(cell) - frac) + 1.0f) * kCell, static_cast<float>(top_) + kHalfCell,
                         scale_ * kFrameDepth);
        }
    }
    DrawBase(pass);
    for (int i = 0; i < 8; ++i) {
        int index = first + i;
        if (index < 0 || index >= count_) continue;
        const HeroFaceCell cell{(*figures_)[index].c_str(), ((static_cast<float>(i) - frac) + 1.0f) * kCell,
                                static_cast<float>(top_) + kHalfCell, scale_ * kFaceDepth, kCell};
        owner_->DrawHeroFace(pass, cell);
    }
}

void CFigureStrip::RebuildList() { count_ = static_cast<int>(figures_->size()); }

void CFigureStrip::LeftDown(int buttons, int x, int y) {
    CUiItemStrip::LeftDown(buttons, x, y);
    if (RegionAt(x, y) != 8) return;
    POINT point{x, y};
    interface_->ScalePointFromScreenEntry(&point);
    int index = static_cast<int>(static_cast<double>(point.x - 50) * kSlotsPerPixel + position_[tab_]);   // _ftol
    if (index < 0 || index >= count_) return;
    interface_->PlaySound(kOnOffSound);
    owner_->ChooseFigure((*figures_)[index]);   // the hero screen: the new hero's figure
}
