// The options window's spin control (UiSpin): constructor, texts and value. See ui.h and
// docs/notes/items_spells.md.

#include "ui.h"
#include "ui_screens.h"
#include "game_strings.h"


#include "lib/crt.h"
#include "lib/rect.h"

namespace {

constexpr int kTextFormat = 1;
constexpr std::uint32_t kTextColour = 0xA7D7E4;

const char* const kOkSound = "buttons\\messbox\\ok.wav";

} // namespace

// The window's virtuals; the input side's click and a left double click that does nothing. Its surface paints the
// current value's text.
UiSpin::UiSpin(Interface3D* ui, Rect newRect, const char* text) : UiWindow(ui) {
    rect = newRect;
    UiSurface* surface = new UiSurface(ui, &newRect, SurfaceBlend::Keyed);
    AddSurface(surface);
    surface->PaintWith([this](UiSurface& out) {
        Rect area = layout;
        area.Offset(-layout.left, -layout.top);
        out.DrawTextIn(texts[value], &area, kTextFormat, kTextColour, 0, 1);
    });
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
    changed = true;
}

void UiSpin::Step(int by) {
    const int count = static_cast<int>(texts.size());
    SetValue(((value + by) % count + count) % count);
}

// --- A click steps the value ------------------------------------------------------------------------

void UiSpin::LeftDown(Point) {
    ui->PlayInterfaceSound(kOkSound);
    SetValue(value + 1);
}
