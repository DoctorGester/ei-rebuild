// The battle screen's slot 1 (00638A10), double click (00638AD0), key release (0063B6D0) and wheel (0063B770): its
// input side slots 1, 7, 11 and 12. See ui.h (CBattleScreen) and docs/notes/ui.md, section 6.
#include "ui.h"
#include "ui_camp.h"

#include "ei/hook.h"
#include "originals_ui.h"

namespace {
// Key actions 0x11..0x16 hold the order keys; each releases its flag in orderKeys_.
constexpr int kFirstOrderKeyAction = 0x11;
// Which orderKeys_ entry each of them releases (the original's jump table at 0063B754).
constexpr int kOrderKeyOfAction[6] = {0, 1, 5, 4, 3, 2};
} // namespace

// --- 00638A10: the battle screen loses the top -----------------------------------------------

void CBattleScreen::OnDeactivate() {
    for (std::uint8_t& key : orderKeys_) key = 0;
    viewPressed_ = false;
    boxSelecting_ = false;
    static_cast<UiContainerInput*>(Input())->OnDeactivate();
}
namespace {
void __fastcall BattleScreenDeactivate(void* input) { WindowFromInput<CBattleScreen>(input)->OnDeactivate(); }
} // namespace
EI_REPLACE(0x00638A10, BattleScreenDeactivate);

// --- 00638AD0: a double click on the battle screen -------------------------------------------

void CBattleScreen::OnLeftDouble(int buttons, int x, int y) {
    if (portraitMode_ != 0) return;
    if (!viewPressed_) {
        UiContainerDoubleClick(InputSide(), nullptr, buttons, x, y);
        if (inputHandled_) return;
    }
    viewPressed_ = true;
    pressX_ = x;
    pressY_ = y;
    boxSelecting_ = false;
}
namespace {
} // namespace
EI_REPLACE_METHOD(0x00638AD0, InputForward<&CBattleScreen::OnLeftDouble>::Call);

// --- 0063B6D0: a key released on the battle screen -------------------------------------------

void CBattleScreen::OnKeyUp(int key, int repeat, int flags) {
    if (portraitMode_ != 0) return;
    InputOf(this)->BaseKeyUp(key, repeat, flags);
    int action = KeyActions()[flags & 0x1FF] - kFirstOrderKeyAction;
    if (action >= 0 && action < 6) orderKeys_[kOrderKeyOfAction[action]] = 0;
}
namespace {
} // namespace
EI_REPLACE_METHOD(0x0063B6D0, InputForward<&CBattleScreen::OnKeyUp>::Call);

// --- 0063B770: the wheel on the battle screen ------------------------------------------------

BOOL CBattleScreen::OnWheel(int flags, int delta, int x, int y) {
    if (portraitMode_ != 0) return 1;
    CUiWindow* const panels[4] = {map_, unitPanel_, messageLog_, topPanel_};
    for (CUiWindow* panel : panels) {
        panel->inputHandled_ = false;
        panel->Input()->Wheel(flags, delta, x, y);
        if (panel->inputHandled_) return 1;
    }
    for (CUiWindow* panel : panels) panel->SetEnabled(0, 0);
    InputOf(this)->BaseWheel(flags, delta, x, y);
    for (CUiWindow* panel : panels) panel->SetEnabled(1, 0);
    return 1;
}
namespace {
} // namespace
EI_REPLACE_METHOD(0x0063B770, InputForward<&CBattleScreen::OnWheel>::Call);
