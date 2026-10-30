// The options window's spin control (UiSpin): constructor, texts and value. See ui.h and
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
UiSpin::UiSpin(Interface3D* ui, RECT newRect, const char* text) : UiWindow(ui) {
    rect = newRect;
    UiSurface* surface = new UiSurface(ui, &newRect, SurfaceBlend::Keyed);
    AddSurface(surface);
    texts.push_back(text ? text : game::kEmptyText);
    changed = false;
    layout = newRect;
    SetValue(0);
}

void UiSpin::AddText(std::string_view text) { texts.emplace_back(text); }

bool UiSpin::TakeChanged() {
    if (!changed) return false;
    changed = false;
    return true;
}

int UiSpin::TakeValue() {
    changed = false;
    return value;
}

void UiSpin::SetValue(int newValue) {
    value = newValue < static_cast<int>(texts.size()) ? newValue : 0;
    UiSurface* surface = surfaces[0];
    surface->Clear();
    RECT area = layout;
    gameapi::OffsetRect(&area, -layout.left, -layout.top);
    // DrawText is Rebirth-patched: through its address.
    surface->DrawTextIn(texts[value], &area, kTextFormat, kTextColour, 0, 1);
    changed = true;
}

// --- A click steps the value ------------------------------------------------------------------------

void UiSpin::LeftDown(int, int, int) {
    ui->PlayInterfaceSound(kOkSound);
    SetValue(value + 1);
}
