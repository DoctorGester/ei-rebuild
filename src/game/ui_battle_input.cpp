// The battle screen's close, double click, key release and wheel: its
// input side. See ui.h (CBattleScreen) and docs/notes/ui.md, section 6.
#include "ui.h"
#include "ui_camp.h"

namespace {
// Key actions 0x11..0x16 hold the order keys; each releases its flag in orderKeys_.
constexpr int kFirstOrderKeyAction = 0x11;
// Which orderKeys_ entry each of them releases.
constexpr int kOrderKeyOfAction[6] = {0, 1, 5, 4, 3, 2};
} // namespace

// --- the battle screen loses the top -------------------------------------------------------------

void CBattleScreen::Deactivate() {
    for (std::uint8_t& key : orderKeys_) key = 0;
    viewPressed_ = false;
    boxSelecting_ = false;
    CUiContainer::Deactivate();
}

// --- a double click on the battle screen ---------------------------------------------------------

void CBattleScreen::LeftDouble(int buttons, int x, int y) {
    if (portraitMode_ != 0) return;
    if (!viewPressed_) {
        CUiContainer::LeftDouble(buttons, x, y);
        if (handled_) return;
    }
    viewPressed_ = true;
    pressX_ = x;
    pressY_ = y;
    boxSelecting_ = false;
}

// --- a key released on the battle screen ---------------------------------------------------------

void CBattleScreen::KeyUp(int key, int repeat, int flags) {
    if (portraitMode_ != 0) return;
    CUiContainer::KeyUp(key, repeat, flags);
    int action = g_keyActions[flags & 0x1FF] - kFirstOrderKeyAction;
    if (action >= 0 && action < 6) orderKeys_[kOrderKeyOfAction[action]] = 0;
}

// --- the wheel on the battle screen --------------------------------------------------------------

void CBattleScreen::Wheel(int flags, int delta, int x, int y) {
    if (portraitMode_ != 0) return;
    CUiWindow* const panels[4] = {map_, unitPanel_, messageLog_, topPanel_};
    for (CUiWindow* panel : panels) {
        panel->handled_ = false;
        panel->Input()->Wheel(flags, delta, x, y);
        if (panel->handled_) return;
    }
    for (CUiWindow* panel : panels) panel->SetEnabled(false, false);
    CUiContainer::Wheel(flags, delta, x, y);
    for (CUiWindow* panel : panels) panel->SetEnabled(true, false);
}
