// The battle screen's close, double click, key release and wheel: its
// input side. See ui.h (CBattleScreen) and docs/notes/ui.md, section 6.
#include "ui.h"
#include "ui_camp.h"

namespace {
// Key actions 0x11..0x16 hold the order keys; each releases its flag in orderKeys.
constexpr int kFirstOrderKeyAction = 0x11;
// Which orderKeys entry each of them releases.
constexpr int kOrderKeyOfAction[6] = {0, 1, 5, 4, 3, 2};
} // namespace

// --- the battle screen loses the top -------------------------------------------------------------

void CBattleScreen::Deactivate() {
    for (std::uint8_t& key : orderKeys) key = 0;
    viewPressed = false;
    boxSelecting = false;
    CUiContainer::Deactivate();
}

// --- a double click on the battle screen ---------------------------------------------------------

void CBattleScreen::LeftDouble(int buttons, int x, int y) {
    if (portraitMode != 0) return;
    if (!viewPressed) {
        CUiContainer::LeftDouble(buttons, x, y);
        if (handled) return;
    }
    viewPressed = true;
    pressX = x;
    pressY = y;
    boxSelecting = false;
}

// --- a key released on the battle screen ---------------------------------------------------------

void CBattleScreen::KeyUp(int key, int repeat, int modifiers) {
    if (portraitMode != 0) return;
    CUiContainer::KeyUp(key, repeat, modifiers);
    int action = g_keyActions[modifiers & 0x1FF] - kFirstOrderKeyAction;
    if (action >= 0 && action < 6) orderKeys[kOrderKeyOfAction[action]] = 0;
}

// --- the wheel on the battle screen --------------------------------------------------------------

void CBattleScreen::Wheel(int modifiers, int delta, int x, int y) {
    if (portraitMode != 0) return;
    CUiWindow* const panels[4] = {map, unitPanel, messageLog, topPanel};
    for (CUiWindow* panel : panels) {
        panel->handled = false;
        panel->Input()->Wheel(modifiers, delta, x, y);
        if (panel->handled) return;
    }
    for (CUiWindow* panel : panels) panel->SetEnabled(false, false);
    CUiContainer::Wheel(modifiers, delta, x, y);
    for (CUiWindow* panel : panels) panel->SetEnabled(true, false);
}
