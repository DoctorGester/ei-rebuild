// The item strips' setups (00621160, 00629A40): the camp's item, spell and part strips and the
// hero screen's figure strips, called by their screens' Creates after constructing the strip.
// See ui_screens.h (CUiItemStrip::Setup, SetupFigures) and docs/notes/ui.md ("Camp, global map,
// hero screens").
#include "ui_screens.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_ui.h"

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
