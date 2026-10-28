// The item strips' setups: the camp's item, spell and part strips and the
// hero screen's figure strips, called by their screens' Creates after constructing the strip;
// the forwarding strip's constructor, and the figure strips' draw, count and click.
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
constexpr int kTextureKind = 3;

// A text surface of the layout rectangle (left, top, right, bottom), format 5 (null
// when the allocation fails).
CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
        return new CUiSurface(ui, left, top, right, bottom, 5, 1);
}

// Layout constants of the strips (the exe's floats).
constexpr float kZero = 0.0f;
constexpr float kCell = 100.0f;
constexpr float kHalfCell = 50.0f;
constexpr float kOne = 1.0f;
constexpr float kFrameDepth = 0.9f;
constexpr float kFaceDepth = 1.5f;
constexpr float kSlotsPerPixel = 0.01f;
constexpr std::uint32_t kDimmed = 0x3E480000;   // 0.1953125 as the end faces' texture offset

// "buttons\battle\on_off.wav": the sound system keeps the game's own string pointer.
const char* OnOffSound() { return "buttons\\battle\\on_off.wav"; }

CUiMesh* Sprite(CUiWindow& w, int index) { return w.SceneMesh(index); }

} // namespace

void CUiItemStrip::Setup(UiPtrArray<CItem>* source, char top) {
    LoadTexture(top ? kTopTexture : kBottomTexture, kTextureKind, nullptr);
    Build(top);
    regions_[0]->SetTip(0x4FB0, 1, 0);
    regions_[1]->SetTip(0x4FB1, 1, 0);
    AddSurface(NewSurface(interface_, 50, top_ + 15, 750, top_ + 35));
    AddSurface(NewSurface(interface_, 50, top_ + 65, 750, top_ + 85));
    slots_ = 9;
    source_ = source;
}

void CUiItemStrip::SetupFigures(MpStringVector* names, char top, char noArrowTips) {
    figures_ = names;
    LoadTexture(kFigureTexture, kTextureKind, nullptr);
    Build(top);
    if (!noArrowTips) {
        regions_[0]->SetTip(0x9DD0, 1, 0);
        regions_[1]->SetTip(0x9DD1, 1, 0);
    }
    tabsEnabled_ = false;
    for (int i = 2; i < 8; ++i) SceneMesh(i)->visible_ = 0;
}

CForwardingStrip::CForwardingStrip(CInterface3D* ui, CItemScreen* owner) : CUiItemStrip(ui, owner) {
}

// --- The figure strips' slots ----------------------------------------------------------------------

BOOL CFigureStrip::Draw(CSceneRenderer* pass) {
    const float position = position_[tab_];
    const int first = static_cast<int>(position);   // _ftol
    const float frac = static_cast<float>(position - static_cast<float>(first));
    Sprite(*this, 0)->textureOffsetU_ = position_[tab_] > kZero ? std::bit_cast<float>(kDimmed) : 0.0f;
    Sprite(*this, 1)->textureOffsetU_ = static_cast<float>(count_ - 7) > position_[tab_] ? std::bit_cast<float>(kDimmed) : 0.0f;
    Sprite(*this, 9)->Place(kHalfCell - frac * kCell, static_cast<float>(top_), scale_ + scale_);
    for (int i = 0; i < 8; ++i) {
        Sprite(*this, 10 + i)->Place(((static_cast<float>(i) - frac) + kOne) * kCell, static_cast<float>(top_) + kHalfCell,
                                      scale_);
    }
    Sprite(*this, 8)->visible_ = 0;
    if (selected_ >= 0 && selected_ < count_) {
        int cell = selected_ - first;
        if (cell >= 0 && cell < 8) {
            Sprite(*this, 8)->visible_ = 1;
            Sprite(*this, 8)->Place(((static_cast<float>(cell) - frac) + kOne) * kCell,
                                     static_cast<float>(top_) + kHalfCell, scale_ * kFrameDepth);
        }
    }
    DrawBase(pass);
    std::string name;
    for (int i = 0; i < 8; ++i) {
        int index = first + i;
        if (index < 0 || index >= count_) continue;
        name = figures_->begin()[index];
        HeroFaceCell cell;
        cell.prototype = name.c_str();
        cell.x = ((static_cast<float>(i) - frac) + kOne) * kCell;
        cell.y = static_cast<float>(top_) + kHalfCell;
        cell.depth = scale_ * kFaceDepth;
        cell.size = kCell;
        owner_->DrawHeroFace(pass, &cell);
    }
    return 1;
}

void CFigureStrip::RebuildList() { count_ = static_cast<int>(figures_->end() - figures_->begin()); }

void CFigureStrip::OnFigureClick(int buttons, int x, int y) {
    OnLButtonDown(buttons, x, y);
    if (RegionAt(x, y) != 8) return;
    POINT point{x, y};
    interface_->ScalePointFromScreenEntry(&point);
    int index = static_cast<int>(static_cast<double>(point.x - 50) * kSlotsPerPixel + position_[tab_]);   // _ftol
    if (index < 0 || index >= count_) return;
    uiorig::PlayUiSound(interface_, OnOffSound(), 0);
    owner_->ChooseFigure(figures_->begin()[index].c_str());   // the hero screen: the new hero's figure
}
