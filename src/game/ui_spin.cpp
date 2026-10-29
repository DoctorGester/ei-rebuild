// The options window's spin control (CUiSpin): constructor, texts and value. See ui.h and
// docs/notes/items_spells.md.

#include "ui.h"
#include "ui_screens.h"
#include "game_strings.h"


#include "lib/crt.h"
#include "lib/gameapi.h"

namespace {

constexpr int kSurfaceFormat = 5;
constexpr int kTextFormat = 1;
constexpr std::uint32_t kTextColour = 0xA7D7E4;

const char* const kOkSound = "buttons\\messbox\\ok.wav";

} // namespace

// The window's virtuals; the input side's click and a left double click that does nothing.
CUiSpin::CUiSpin(CInterface3D* ui, RECT rect, const char* text) : CUiWindow(ui) {
    rect_ = rect;
    CUiSurface* surface = new CUiSurface(ui, &rect, kSurfaceFormat);
    AddSurface(surface);
    texts_.push_back(text ? text : game::kEmptyText);
    changed_ = false;
    layout_ = rect;
    SetValue(0);
}

void CUiSpin::AddText(std::string_view text) { texts_.emplace_back(text); }

bool CUiSpin::GetValue(int* value) {
    *value = value_;
    if (!changed_) return false;
    changed_ = false;
    return true;
}

void CUiSpin::SetValue(int value) {
    value_ = value < static_cast<int>(texts_.size()) ? value : 0;
    CUiSurface* surface = surfaces_[0];
    surface->Clear();
    RECT rect = layout_;
    gameapi::OffsetRect(&rect, -layout_.left, -layout_.top);
    // DrawText is Rebirth-patched: through its address.
    surface->DrawTextIn(texts_[value_], &rect, kTextFormat, kTextColour, 0, 1);
    changed_ = true;
}

// --- A click steps the value ------------------------------------------------------------------------

void CUiSpin::LeftDown(int, int, int) {
    interface_->PlaySound(kOkSound);
    SetValue(value_ + 1);
}
