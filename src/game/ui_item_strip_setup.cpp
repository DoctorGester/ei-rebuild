// The item strips' setups (00621160, 00629A40): the camp's item, spell and part strips and the
// hero screen's figure strips, called by their screens' Creates after constructing the strip;
// and the figure strips' draw, count and click (00629AF0, 00629AD0, 00629DB0).
// See ui_screens.h (CUiItemStrip::Setup, SetupFigures) and docs/notes/ui.md ("Camp, global map,
// hero screens").
#include "ui_item_info.h"
#include "ui_screens.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ui.h"

#include <bit>
#include <cstdint>

namespace {

constexpr const char* kTopTexture = "Inventory02";        // 00793CDC
constexpr const char* kBottomTexture = "Inventory01";     // 00793CD0
constexpr const char* kFigureTexture = "InventoryM01";    // 00793F58
constexpr int kTextureKind = 3;

// A text surface of the layout rectangle (left, top, right, bottom), format 5 (0x38 bytes; null
// when the allocation fails).
CUiSurface* NewSurface(CInterface3D* ui, int left, int top, int right, int bottom) {
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    if (!memory) return nullptr;
    return static_cast<CUiSurface*>(memory)->Construct(ui, left, top, right, bottom, 5, 1);
}

// Layout constants of the strips (the exe's floats at 0073B858, 0073BAB0, 0074023C, 0073B84C,
// 0073CD70, 0073C51C, 0073D09C).
constexpr float kZero = 0.0f;
constexpr float kCell = 100.0f;
constexpr float kHalfCell = 50.0f;
constexpr float kOne = 1.0f;
constexpr float kFrameDepth = 0.9f;
constexpr float kFaceDepth = 1.5f;
constexpr float kSlotsPerPixel = 0.01f;
constexpr std::uint32_t kDimmed = 0x3E480000;   // 0.1953125 as the end faces' texture offset

// 005ED7B0 through its address (CItemScreen::DrawHeroFace; its verify wrapper runs).
__declspec(naked) void __fastcall DrawHeroFaceAt(CUiWindow* /*owner*/, void*, int /*pass*/, HeroFaceCell*)
    EI_ORIGINAL(0x005ED7B0)

// "buttons\battle\on_off.wav": the sound system keeps the game's own string pointer.
const char* OnOffSound() { return reinterpret_cast<const char*>(0x007935BC); }

// The owner's virtual slot 52 (the hero screen: the figure chosen for the new hero).
void ChooseFigure(CUiWindow* owner, const char* name) {
    using Fn = void(__thiscall*)(CUiWindow*, const char*);
    reinterpret_cast<Fn>(owner->vtable_[52])(owner, name);
}

// The original keeps this value in a float local (FSTP dword) and reads it back.
float StoreFloat(float value) {
    volatile float stored = value;
    return stored;
}

CUiMesh* Sprite(CUiWindow& w, int index) { return reinterpret_cast<CUiMesh*>(w.sceneObjects_.data[index]); }

} // namespace

void CUiItemStrip::Setup(UiPtrArray<char>* source, char top) {
    window.LoadTexture(top ? kTopTexture : kBottomTexture, kTextureKind, nullptr);
    ItemStripBuildOriginal(this, nullptr, top);
    window.regions_.data[0]->SetTip(0x4FB0, 1, 0);
    window.regions_.data[1]->SetTip(0x4FB1, 1, 0);
    window.AddSurface(NewSurface(window.interface_, 50, top_ + 15, 750, top_ + 35));
    window.AddSurface(NewSurface(window.interface_, 50, top_ + 65, 750, top_ + 85));
    slots_ = 9;
    source_ = source;
}
EI_REPLACE_METHOD(0x00621160, CUiItemStrip::Setup);

void CUiItemStrip::SetupFigures(MpStringVector* names, char top, char noArrowTips) {
    figures_ = names;
    window.LoadTexture(kFigureTexture, kTextureKind, nullptr);
    ItemStripBuildOriginal(this, nullptr, top);
    if (!noArrowTips) {
        window.regions_.data[0]->SetTip(0x9DD0, 1, 0);
        window.regions_.data[1]->SetTip(0x9DD1, 1, 0);
    }
    tabsEnabled_ = false;
    for (int i = 2; i < 8; ++i) reinterpret_cast<CUiMesh*>(window.sceneObjects_.data[i])->visible_ = 0;
}
EI_REPLACE_METHOD(0x00629A40, CUiItemStrip::SetupFigures);

// --- The figure strips' slots ----------------------------------------------------------------------

BOOL CUiItemStrip::DrawFigures(int pass) {
    const float position = position_[tab_];
    const int first = static_cast<int>(position);   // _ftol
    const float frac = StoreFloat(position - static_cast<float>(first));
    Sprite(window, 0)->uOffset_ = position_[tab_] > kZero ? std::bit_cast<float>(kDimmed) : 0.0f;
    Sprite(window, 1)->uOffset_ = static_cast<float>(count_ - 7) > position_[tab_] ? std::bit_cast<float>(kDimmed) : 0.0f;
    Sprite(window, 9)->Place(kHalfCell - frac * kCell, static_cast<float>(top_), scale_ + scale_);
    for (int i = 0; i < 8; ++i) {
        Sprite(window, 10 + i)->Place(((static_cast<float>(i) - frac) + kOne) * kCell, static_cast<float>(top_) + kHalfCell,
                                      scale_);
    }
    Sprite(window, 8)->visible_ = 0;
    if (selected_ >= 0 && selected_ < count_) {
        int cell = selected_ - first;
        if (cell >= 0 && cell < 8) {
            Sprite(window, 8)->visible_ = 1;
            Sprite(window, 8)->Place(((static_cast<float>(cell) - frac) + kOne) * kCell,
                                     static_cast<float>(top_) + kHalfCell, scale_ * kFrameDepth);
        }
    }
    window.DrawBase(pass);
    mfc::CString name;
    for (int i = 0; i < 8; ++i) {
        int index = first + i;
        if (index < 0 || index >= count_) continue;
        name = *reinterpret_cast<const mfc::CString*>(&figures_->begin()[index]);
        HeroFaceCell cell;
        cell.prototype = name;
        cell.x = ((static_cast<float>(i) - frac) + kOne) * kCell;
        cell.y = static_cast<float>(top_) + kHalfCell;
        cell.depth = scale_ * kFaceDepth;
        cell.size = kCell;
        DrawHeroFaceAt(owner_, nullptr, pass, &cell);
    }
    return 1;
}
EI_REPLACE_METHOD(0x00629AF0, CUiItemStrip::DrawFigures);

void CUiItemStrip::CountFigures() { count_ = static_cast<int>(figures_->end() - figures_->begin()); }
EI_REPLACE_METHOD(0x00629AD0, CUiItemStrip::CountFigures);

void CUiItemStrip::OnFigureClick(int buttons, int x, int y) {
    OnLButtonDown(buttons, x, y);
    if (window.RegionAt(x, y) != 8) return;
    POINT point{x, y};
    window.interface_->CallScalePointFromScreen(&point);
    int index = static_cast<int>(static_cast<double>(point.x - 50) * kSlotsPerPixel + position_[tab_]);   // _ftol
    if (index < 0 || index >= count_) return;
    uiorig::PlayUiSound(window.interface_, OnOffSound(), 0);
    ChooseFigure(owner_, figures_->begin()[index]);
}
namespace {
void __fastcall FigureStripClick(void* input, void*, int buttons, int x, int y) {
    WindowFromInput<CUiItemStrip>(input)->OnFigureClick(buttons, x, y);
}
} // namespace
EI_REPLACE(0x00629DB0, FigureStripClick);
