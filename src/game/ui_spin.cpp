// The options window's spin control (CUiSpin): constructor, texts and value. See ui.h and
// docs/notes/items_spells.md.

#include "ui.h"
#include "ui_screens.h"
#include "game_strings.h"


#include "lib/crt.h"
#include "lib/gameapi.h"

namespace {

constexpr int kTextFormat = 1;
constexpr std::uint32_t kTextColour = 0xA7D7E4;

const char* const kOkSound = "buttons\\messbox\\ok.wav";

} // namespace

// The window's virtuals; the input side's click and a left double click that does nothing.
CUiSpin::CUiSpin(CInterface3D* ui, RECT newRect, const char* text) : CUiWindow(ui) {
    rect = newRect;
    CUiSurface* surface = new CUiSurface(ui, &newRect, SurfaceBlend::Keyed);
    AddSurface(surface);
    texts.push_back(text ? text : game::kEmptyText);
    changed = false;
    layout = newRect;
    SetValue(0);
}

void CUiSpin::AddText(std::string_view text) { texts.emplace_back(text); }

bool CUiSpin::TakeChanged() {
    if (!changed) return false;
    changed = false;
    return true;
}

int CUiSpin::TakeValue() {
    changed = false;
    return value;
}

void CUiSpin::SetValue(int newValue) {
    value = newValue < static_cast<int>(texts.size()) ? newValue : 0;
    CUiSurface* surface = surfaces[0];
    surface->Clear();
    RECT area = layout;
    gameapi::OffsetRect(&area, -layout.left, -layout.top);
    // DrawText is Rebirth-patched: through its address.
    surface->DrawTextIn(texts[value], &area, kTextFormat, kTextColour, 0, 1);
    changed = true;
}

// --- A click steps the value ------------------------------------------------------------------------

void CUiSpin::LeftDown(int, int, int) {
    ui->PlaySound(kOkSound);
    SetValue(value + 1);
}
