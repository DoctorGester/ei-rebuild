// The options window's spin control (UiSpin): constructor, texts and value. See ui.h and
// docs/notes/items_spells.md.

#include "ui.h"
#include "ui_screens.h"


#include "lib/crt.h"
#include "lib/rect.h"

namespace {

constexpr int kFont = 1;


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
        out.DrawTextIn(texts[value], area, TextStyle{kFont, kUiTextColour});
    });
    texts.push_back(text ? text : "");
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
    ui->PlayInterfaceSound(kMessageBoxOkSound);
    SetValue(value + 1);
}
