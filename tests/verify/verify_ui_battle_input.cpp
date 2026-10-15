// Checks for the battle screen's double click, key release and wheel (ui_battle_input.cpp). All run
// ours only: each hands the event to the container's dispatch, whose panels and extra input sides
// must not get it twice.
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_screens.h"

#include <cstring>

namespace {
void* s_unused;
}

// CBattleScreenSetup::OnLeftDouble (00638AD0), as the button-down's check: without the portrait
// pick, a press already on, or one no panel took, leaves the press on at (x, y) without a drag; a
// press a panel took leaves it off. Reached by ui_container_input's double click on the view.
static void __fastcall BattleLeftDoubleVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CBattleScreenSetup>(input);
    bool portraits = self->portraitMode_ != 0;
    bool pressed = self->viewPressed_;
    self->CBattleScreenSetup::OnLeftDouble(buttons, x, y);
    if (portraits) return;
    bool started = self->viewPressed_ && self->pressX_ == x && self->pressY_ == y && !self->boxSelecting_;
    bool ok = pressed ? started : (self->inputHandled_ ? !self->viewPressed_ : started);
    ei::verify::Check("CBattleScreenSetup::OnLeftDouble", true, ok, "(%d,%d) pressed %d handled %d press %d drag %d", x,
                      y, pressed ? 1 : 0, self->inputHandled_ ? 1 : 0, self->viewPressed_ ? 1 : 0,
                      self->boxSelecting_ ? 1 : 0);
}
EI_VERIFY(0x00638AD0, BattleLeftDoubleVerify, 0, s_unused);

// CBattleScreenSetup::OnKeyUp (0063B6D0): the order key of the key's action (0x11..0x16) is clear
// afterwards and the other five keep their value (the panels' key-up handlers do not touch them).
// Reached by every key released over the battle screen (camera, ui_hud's TAB, ...).
static void __fastcall BattleKeyUpVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CBattleScreenSetup>(input);
    bool portraits = self->portraitMode_ != 0;
    std::uint8_t before[6];
    std::memcpy(before, self->orderKeys_, 6);
    self->CBattleScreenSetup::OnKeyUp(key, repeat, flags);
    static constexpr int kIndex[6] = {0, 1, 5, 4, 3, 2};
    int action = KeyActions()[flags & 0x1FF] - 0x11;
    if (!portraits && action >= 0 && action < 6) before[kIndex[action]] = 0;
    bool ok = std::memcmp(before, self->orderKeys_, 6) == 0;
    ei::verify::Check("CBattleScreenSetup::OnKeyUp", true, ok, "key %d flags %X action %d", key, flags, action + 0x11);
}
EI_VERIFY(0x0063B6D0, BattleKeyUpVerify, 0, s_unused);

// CBattleScreenSetup::OnWheel (0063B770): returns 1; the map, unit panel, message log and top
// panel are enabled afterwards whenever ours reached the container's dispatch (none of them took
// the wheel). Reached by ui_hud's wheel over the map.
static BOOL __fastcall BattleWheelVerify(void* input, void*, int flags, int delta, int x, int y) {
    auto* self = WindowFromInput<CBattleScreenSetup>(input);
    bool portraits = self->portraitMode_ != 0;
    BOOL result = self->CBattleScreenSetup::OnWheel(flags, delta, x, y);
    bool ok = result == 1;
    CUiWindow* const panels[4] = {self->map_, self->unitPanel_, self->messageLog_, self->topPanel_};
    bool taken = false;
    for (CUiWindow* panel : panels) taken |= panel->inputHandled_;
    if (!portraits && !taken)
        for (CUiWindow* panel : panels) ok &= panel->enabled_ == 1;
    ei::verify::Check("CBattleScreenSetup::OnWheel", true, ok, "delta %d at (%d,%d) result %d taken %d", delta, x, y,
                      result, taken ? 1 : 0);
    return result;
}
EI_VERIFY(0x0063B770, BattleWheelVerify, 0, s_unused);
