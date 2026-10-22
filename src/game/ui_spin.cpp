// The options window's spin control (CUiSpin, 0065BCD0-0065BFC3): constructor, texts and value.
// See ui.h and docs/notes/items_spells.md (A-H19).

#include "ui.h"
#include "ui_window_class.h"
#include "ui_screens.h"
#include "game_strings.h"

#include "originals_ui.h"

#include "lib/crt.h"
#include "lib/gameapi.h"

#include "ei/hook.h"
#include "game_data.h"

namespace {

// The compiled tables (C19, ui_window_class.h) in place of game.exe's 00742D5C / 00742D28: the window's slots; the
// input side's click (2), and a left double click that does nothing (7: 0065C000, RET 0xC).
struct SpinTable final : UiWindowClass<SpinTable, &CUiSpin::SpinDelete> {};
struct SpinInputTable final : InputSideClass<SpinInputTable, &CUiSpin::SpinDelete> {
    int LeftDown(int b, int x, int y) override { return SideWindow()->OnLButtonDown(b, x, y), 0; }   // 2: 0065BFD0
    int LeftDouble(int, int, int) override { return 0; }                                           // 7: 0065C000
};
constexpr int kSurfaceFormat = 5;
constexpr int kTextFormat = 1;
constexpr std::uint32_t kTextColour = 0xA7D7E4;

} // namespace

void** CUiSpin::Vtable() { return SpinTable::Slots(); }
void** CUiSpin::InputVtable() { return SpinInputTable::Slots(); }

CUiSpin* CUiSpin::Construct(CInterface3D* ui, RECT rect, const char* text) {
    CUiWindow::Construct(ui);
    texts_.Construct();
    vtable_ = Vtable();
    inputVtable_ = InputVtable();
    rect_ = rect;
    CUiSurface* memory = gamecrt::operator_new_as<CUiSurface>();
    AddSurface(memory ? memory->ConstructOnRect(ui, &rect, kSurfaceFormat) : nullptr);
    texts_.SetAtGrow(texts_.size, text ? text : game::EmptyText());
    changed_ = false;
    layout_ = rect;
    SetValue(0);
    return this;
}
EI_REWRITTEN(0x0065BCD0, CUiSpin::Construct);

void CUiSpin::AddText(const char* text) { texts_.SetAtGrow(texts_.size, text ? text : game::EmptyText()); }
EI_REWRITTEN(0x0065BE70, CUiSpin::AddText);

bool CUiSpin::GetValue(int* value) {
    *value = value_;
    if (!changed_) return false;
    changed_ = false;
    return true;
}
EI_REWRITTEN(0x0065BEB0, CUiSpin::GetValue);

void CUiSpin::SetValue(int value) {
    value_ = value < texts_.size ? value : 0;
    CUiSurface* surface = surfaces_.data[0];
    UiSurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    RECT rect = layout_;
    gameapi::OffsetRect(&rect, -layout_.left, -layout_.top);
    // DrawText is Rebirth-patched: through its address.
    surface->DrawTextIn(texts_.data[value_], &rect, kTextFormat, kTextColour, 0, 1);
    UiSurfaceReleaseDC(surface);
    changed_ = true;
}
EI_REWRITTEN(0x0065BEE0, CUiSpin::SetValue);

// --- 0065BFD0 (A-H25): a click steps the value ----------------------------------------------------

namespace {
const char* const kOkSound = "buttons\\messbox\\ok.wav";
} // namespace

void CUiSpin::OnLButtonDown(int, int, int) {
    interface_->PlaySound(kOkSound, 0);
    SetValue(value_ + 1);
}

EI_REWRITTEN(0x0065BFD0, InputForward<&CUiSpin::OnLButtonDown>::Call);
