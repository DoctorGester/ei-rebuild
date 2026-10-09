// The options window's spin control (CUiSpin, 0065BCD0-0065BFC3): constructor, texts and value.
// See ui.h and docs/notes/items_spells.md (A-H19).

#include "ui.h"

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

// The exe's empty text (0079B298).
const char* EmptyText() { return reinterpret_cast<const char*>(std::uintptr_t(0x0079B298)); }

} // namespace

CUiSpin* CUiSpin::Construct(CInterface3D* ui, RECT rect, const char* text) {
    CUiWindow::Construct(ui);
    texts_.Construct();
    vtable_ = reinterpret_cast<void**>(kSpinVtable);
    inputVtable_ = reinterpret_cast<void**>(kSpinInputVtable);
    rect_ = rect;
    void* memory = gamecrt::operator_new(sizeof(CUiSurface));
    AddSurface(memory ? SurfaceConstructOnRectOriginal(memory, nullptr, ui, &rect, kSurfaceFormat) : nullptr);
    texts_.SetAtGrow(texts_.size, text ? text : EmptyText());
    changed_ = false;
    layout_ = rect;
    SetValue(0);
    return this;
}
EI_REPLACE_METHOD(0x0065BCD0, CUiSpin::Construct);

void CUiSpin::AddText(const char* text) { texts_.SetAtGrow(texts_.size, text ? text : EmptyText()); }
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
    SurfaceGetDC(surface);
    SurfaceClearOriginal(surface);
    RECT rect = layout_;
    gameapi::OffsetRect(&rect, -layout_.left, -layout_.top);
    // DrawText is Rebirth-patched: through its address.
    SurfaceDrawText(surface, nullptr, texts_.data[value_], &rect, kTextFormat, kTextColour, 0, 1);
    SurfaceReleaseDC(surface);
    changed_ = true;
}
EI_REPLACE_METHOD(0x0065BEE0, CUiSpin::SetValue);
