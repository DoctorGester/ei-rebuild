// Checks for the item strips' setups (ui_item_strip_setup.cpp) and the trade panel's price text
// (00626090, ui_trade_panel.cpp).
//
// Setup / SetupFigures build the strip (Build's own wrapper runs side by side inside) and add
// surfaces and tooltips: ours only. After Setup: two more surfaces, nine slots, the source list,
// tooltips on both arrows. After SetupFigures: the name list, tabs off, the tab buttons' sprites
// (scene objects 2..7) hidden, the arrows' tooltips unless asked not to. Setup: ui_camp (the camp's
// three strips); SetupFigures: ui_hero_screen (the hero screen's two figure strips).
//
// FormatPrice is pure: the original into a scratch string, ours into `out`; the texts must agree.
// Reached by the trade panel's DrawLists (ui_camp: page 1).
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_screens.h"
#include "../../src/lib/mfc.h"

#include <cstring>

namespace {
void* s_unused;

bool HasTips(const CUiWindow& w) {
    return w.regions_.size >= 2 && w.regions_.data[0]->tip_ && w.regions_.data[1]->tip_;
}
} // namespace

// CUiItemStrip::Setup (00621160): ours only.
static void __fastcall StripSetupVerify(CUiItemStrip* self, void*, UiPtrArray<char>* source, char top) {
    int surfaces = self->window.surfaces_.size;
    self->CUiItemStrip::Setup(source, top);
    const CUiWindow& w = self->window;
    ei::verify::Check("CUiItemStrip::Setup", true,
                      w.surfaces_.size == surfaces + 2 && self->slots_ == 9 && self->source_ == source && HasTips(w),
                      "top %d surfaces %d -> %d slots %d tips %d", top, surfaces, w.surfaces_.size, self->slots_,
                      HasTips(w) ? 1 : 0);
}
EI_VERIFY(0x00621160, StripSetupVerify, 0, s_unused);

// CUiItemStrip::SetupFigures (00629A40): ours only.
static void __fastcall StripSetupFiguresVerify(CUiItemStrip* self, void*, MpStringVector* names, char top,
                                               char noArrowTips) {
    self->CUiItemStrip::SetupFigures(names, top, noArrowTips);
    const CUiWindow& w = self->window;
    bool hidden = w.sceneObjects_.size >= 8;
    for (int i = 2; hidden && i < 8; ++i) hidden = reinterpret_cast<CUiMesh*>(w.sceneObjects_.data[i])->visible_ == 0;
    ei::verify::Check("CUiItemStrip::SetupFigures", true,
                      self->figures_ == names && !self->tabsEnabled_ && hidden && (noArrowTips || HasTips(w)),
                      "top %d noTips %d tabs %d hidden %d", top, noArrowTips, self->tabsEnabled_ ? 1 : 0,
                      hidden ? 1 : 0);
}
EI_VERIFY(0x00629A40, StripSetupFiguresVerify, 0, s_unused);

// CTradePanel::FormatPrice (00626090): MOV ECX,[ESP+4] (4); TEST ECX,ECX (2).
static void(__fastcall* s_formatPrice)(CTradePanel*, void*, int, mfc::CString*);
static void __fastcall FormatPriceVerify(CTradePanel* self, void*, int price, mfc::CString* out) {
    mfc::CString expected;
    s_formatPrice(self, nullptr, price, &expected);
    self->CTradePanel::FormatPrice(price, out);
    const char* e = expected;
    const char* a = *out;
    ei::verify::Check("CTradePanel::FormatPrice", true, std::strcmp(e, a) == 0, "%d: \"%s\" / \"%s\"", price, e, a);
}
EI_VERIFY(0x00626090, FormatPriceVerify, 6, s_formatPrice);
