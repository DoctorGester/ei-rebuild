// The options window's spin control (CUiSpin, 0065BCD0-0065BFC3): constructor, texts and value.
// See ui.h and docs/notes/items_spells.md (A-H19).

#include "ui.h"
#include "game_strings.h"

#include "originals_ui.h"

#include "lib/crt.h"
#include "lib/gameapi.h"

#include "ei/hook.h"

namespace {

constexpr std::uintptr_t kSpinVtable = std::uintptr_t(0x00742D5C);
constexpr std::uintptr_t kSpinInputVtable = std::uintptr_t(0x00742D28);
constexpr int kSurfaceFormat = 5;
constexpr int kTextFormat = 1;
constexpr std::uint32_t kTextColour = 0xA7D7E4;

} // namespace

CUiSpin* CUiSpin::Construct(CInterface3D* ui, RECT rect, const char* text) {
    CUiWindow::Construct(ui);
    texts_.Construct();
    vtable_ = reinterpret_cast<void**>(kSpinVtable);
    inputVtable_ = reinterpret_cast<void**>(kSpinInputVtable);
    rect_ = rect;
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? static_cast<CUiSurface*>(memory)->ConstructOnRect(ui, &rect, kSurfaceFormat) : nullptr);
    texts_.SetAtGrow(texts_.size, text ? text : game::EmptyText());
    changed_ = false;
    layout_ = rect;
    SetValue(0);
    return this;
}
EI_REPLACE_METHOD(0x0065BCD0, CUiSpin::Construct);

void CUiSpin::AddText(const char* text) { texts_.SetAtGrow(texts_.size, text ? text : game::EmptyText()); }
EI_REPLACE_METHOD(0x0065BE70, CUiSpin::AddText);

bool CUiSpin::GetValue(int* value) {
    *value = value_;
    if (!changed_) return false;
    changed_ = false;
    return true;
}
EI_REPLACE_METHOD(0x0065BEB0, CUiSpin::GetValue);

void CUiSpin::SetValue(int value) {
    value_ = value < texts_.size ? value : 0;
    CUiSurface* surface = surfaces_.data[0];
    UiSurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    RECT rect = layout_;
    gameapi::OffsetRect(&rect, -layout_.left, -layout_.top);
    // DrawText is Rebirth-patched: through its address.
    SurfaceDrawText(surface, nullptr, texts_.data[value_], &rect, kTextFormat, kTextColour, 0, 1);
    UiSurfaceReleaseDC(surface);
    changed_ = true;
}
EI_REPLACE_METHOD(0x0065BEE0, CUiSpin::SetValue);

// --- 0065BFD0 (A-H25): a click steps the value ----------------------------------------------------

namespace {
const char* const kOkSound = "buttons\\messbox\\ok.wav";
} // namespace

void CUiSpin::OnLButtonDown(int, int, int) {
    interface_->PlaySound(kOkSound, 0);
    SetValue(value_ + 1);
}

namespace {
void __fastcall SpinLeftDown(void* input, void*, int flags, int x, int y) {
    reinterpret_cast<CUiSpin*>(static_cast<std::uint8_t*>(input) - offsetof(CUiWindow, inputVtable_))
        ->OnLButtonDown(flags, x, y);
}
} // namespace

EI_REPLACE(0x0065BFD0, SpinLeftDown);
