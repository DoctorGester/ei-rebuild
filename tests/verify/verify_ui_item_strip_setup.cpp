// Checks for the item strips' setups (ui_item_strip_setup.cpp) and the trade panel's price text
// (00626090, ui_trade_panel.cpp).
//
// Setup / SetupFigures build the strip (Build's own wrapper runs side by side inside) and add
// surfaces and tooltips: ours only. After Setup: two more surfaces, nine slots, the source list,
// tooltips on both arrows. After SetupFigures: the name list, tabs off, the tab buttons' sprites
// (scene objects 2..7) hidden, the arrows' tooltips unless asked not to. Setup: ui_camp (the camp's
// three strips); SetupFigures: ui_hero_screen (the hero screen's two figure strips).
//
// The figure strips' slots: CountFigures side by side (count_ only); DrawFigures and the click ours
// only (they draw / pick a figure through the owner): the draw returns 1 with the end faces dimmed
// and the frame shown exactly as the position and selected_ ask; the click leaves the handled flag
// set when the point is in a region. ui_hero_screen (mode 10 shows both strips; it clicks each).
//
// FormatPrice is pure: the original into a scratch string, ours into `out`; the texts must agree.
// Reached by the trade panel's DrawLists (ui_camp: page 1).
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_screens.h"
#include "../../src/lib/mfc.h"

#include <cstring>
#include <bit>

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

// The figure strips' count (00629AD0): MOV EAX,[ECX+0x194] (6).
static void(__fastcall* s_countFigures)(CUiItemStrip*, void*);
static void __fastcall CountFiguresVerify(CUiItemStrip* self, void*) {
    s_countFigures(self, nullptr);
    int expected = self->count_;
    self->count_ = -12345;
    self->CUiItemStrip::CountFigures();
    ei::verify::Check("CUiItemStrip::CountFigures", expected, self->count_, "names");
}
EI_VERIFY(0x00629AD0, CountFiguresVerify, 6, s_countFigures);

// The figure strips' draw (00629AF0): ours only.
static BOOL __fastcall DrawFiguresVerify(CUiItemStrip* self, void*, int pass) {
    BOOL result = self->CUiItemStrip::DrawFigures(pass);
    const CUiWindow& w = self->window;
    auto sprite = [&](int i) { return reinterpret_cast<const CUiMesh*>(w.sceneObjects_.data[i]); };
    const float position = self->position_[self->tab_];
    const int first = static_cast<int>(position);
    const std::uint32_t dim = 0x3E480000;
    std::uint32_t left = std::bit_cast<std::uint32_t>(sprite(0)->textureOffsetU_);
    std::uint32_t right = std::bit_cast<std::uint32_t>(sprite(1)->textureOffsetU_);
    int cell = self->selected_ - first;
    int shown = self->selected_ >= 0 && self->selected_ < self->count_ && cell >= 0 && cell < 8 ? 1 : 0;
    ei::verify::Check("CUiItemStrip::DrawFigures", true,
                      result == 1 && left == (position > 0.0f ? dim : 0u) &&
                          right == (static_cast<float>(self->count_ - 7) > position ? dim : 0u) &&
                          sprite(8)->visible_ == shown,
                      "position %g count %d selected %d: faces %08X %08X frame %d", position, self->count_,
                      self->selected_, left, right, sprite(8)->visible_);
    return result;
}
EI_VERIFY(0x00629AF0, DrawFiguresVerify, 0, s_unused);

// The figure strips' click (00629DB0, input slot 2): ours only.
static void __fastcall FigureClickVerify(void* input, void*, int buttons, int x, int y) {
    CUiItemStrip* self = WindowFromInput<CUiItemStrip>(input);
    self->CUiItemStrip::OnFigureClick(buttons, x, y);
    int region = self->window.RegionAt(x, y);
    if (region >= 0) {
        ei::verify::Check("CUiItemStrip::OnFigureClick", true, UiHandled(self), "region %d at %d, %d", region, x, y);
    }
}
EI_VERIFY(0x00629DB0, FigureClickVerify, 0, s_unused);
