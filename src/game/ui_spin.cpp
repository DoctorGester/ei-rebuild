// The options window's spin control (ui_spin.h): constructor, destructor, texts and value. See docs/notes/ui.md,
// section 8.
#include "game/ui_spin.h"
#include "game/ui.h"
#include "game/ui_screens.h"
#include "lib/crt.h"
#include "lib/rect.h"

namespace {
constexpr int kFont = 1;
} // namespace

// The window's virtuals; its click and a left double click that does nothing. Its surface paints the
// current value's text.
UiSpin::UiSpin(Rect newRect, const char* text) {
    rect = newRect;
    UiSurface* surface = AddSurface(std::make_unique<UiSurface>(&newRect, SurfaceBlend::Keyed));
    surface->PaintWith([this](UiSurface& out) {
        Rect area = layout;
        area.Offset(-layout.left, -layout.top);
        out.DrawTextIn(texts[value], area, TextStyle{kFont, kUiTextColour});
    });
    texts.push_back(text ? text : "");
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

void UiSpin::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: LeftDown(event); break;
    case UiEventKind::LeftDouble: break;   // a double click does nothing
    default: break;
    }
}

void UiSpin::LeftDown(UiEvent&) {
    g_interface.PlayInterfaceSound(kMessageBoxOkSound);
    SetValue(value + 1);
}

UiSpin::~UiSpin() = default;
