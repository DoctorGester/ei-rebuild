// Check for the battle screen's key-down handler (ui_battle_keys.cpp; 0063A560, Rebirth's
// MPSpeedControl reproduced). Ours only: it plays sounds, opens windows, orders units through the
// local view and hands the key to the panels, none of which may happen twice.
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_screens.h"

#include <cstring>

namespace {
void* s_unused;
}

// CBattleScreenSetup::OnKeyDown (0063A560). Read back: while a portrait click picks a player only
// Esc changes anything (it ends the pick); otherwise each order key keeps its value, is cleared
// (a window opened over the screen deactivates it, 00638A10) or, for the key of the pressed
// action (0x11..0x16), is set. Nothing is read after a quick save / load key (0x35 / 0x36).
// Reached by every key pressed over the battle screen in the vanilla launch (the camera keys,
// ui_hud's TAB, ...); the worker that wrote it could not run the game.
static void __fastcall BattleKeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CBattleScreenSetup>(input);
    const int portraits = self->portraitMode_;
    const int action = KeyActions()[flags & 0x1FF];
    std::uint8_t before[6];
    std::memcpy(before, self->orderKeys_, 6);
    self->CBattleScreenSetup::OnKeyDown(key, repeat, flags);
    if (portraits != 0) {
        ei::verify::Check("CBattleScreenSetup::OnKeyDown portrait pick", key == 0x1B ? 0 : portraits, self->portraitMode_,
                          "key %d", key);
        ei::verify::Check("CBattleScreenSetup::OnKeyDown portrait pick keys", true,
                          std::memcmp(before, self->orderKeys_, 6) == 0, "key %d", key);
        return;
    }
    if (action == 0x35 || action == 0x36) return;
    static constexpr int kIndex[6] = {0, 1, 5, 4, 3, 2};
    const int held = action >= 0x11 && action <= 0x16 ? kIndex[action - 0x11] : -1;
    bool ok = true;
    for (int i = 0; i < 6; ++i) {
        const std::uint8_t now = self->orderKeys_[i];
        ok &= now == before[i] || now == 0 || (i == held && now == 1);
    }
    ei::verify::Check("CBattleScreenSetup::OnKeyDown", true, ok, "key %d flags %X action %d", key, flags, action);
}
EI_VERIFY(0x0063A560, BattleKeyDownVerify, 0, s_unused);
