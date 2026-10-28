// The mouse cursor's set-up and reset: the cursor object is initialised and given its 25 shapes, the "sm_"
// set when the small cursors are on. The cursor object itself stays the game's own code.
#include "ui_cursor.h"
#include "game_singletons.h"
#include "model.h"

#include <cstdint>

namespace {
// The cursor object and its methods.
// The device the cursor is drawn on.
// The set-up function through its address (the verify build's wrapper sees the call from the reset).
void SetUpCursorsAt(bool modeTwo, CDirectInputMouse* context, int smallCursors, int flags) {
    SetUpCursors(modeTwo, context, smallCursors, flags);
}

struct CursorShape {
    const char* name;
    const char* smallName;
    int hotX, hotY;
};
// In the shape order the game selects them by.
const CursorShape kShapes[] = {
    {"cursor_default", "sm_cursor_default", 0, 0},
    {"cursor_attack", "sm_cursor_attack", 0, 0},
    {"cursor_spell", "sm_cursor_spell", 0, 0},
    {"cursor_wait", "sm_cursor_wait", 0, 0},
    {"cursor_use", "sm_cursor_use", 0, 0},
    {"cursor_scroll_u", "sm_cursor_scroll_u", 16, 0},
    {"cursor_scroll_ur", "sm_cursor_scroll_ur", 32, 0},
    {"cursor_scroll_r", "sm_cursor_scroll_r", 32, 16},
    {"cursor_scroll_dr", "sm_cursor_scroll_dr", 32, 32},
    {"cursor_scroll_d", "sm_cursor_scroll_d", 16, 32},
    {"cursor_scroll_dl", "sm_cursor_scroll_dl", 0, 32},
    {"cursor_scroll_l", "sm_cursor_scroll_l", 0, 16},
    {"cursor_scroll_ul", "sm_cursor_scroll_ul", 0, 0},
    {"cursor_cancel", "sm_cursor_cancel", 0, 0},
    {"cursor_talk", "sm_cursor_talk", 0, 0},
    {"cursor_move", "sm_cursor_move", 0, 0},
    {"cursor_attack_hd", "sm_cursor_attack_hd", 0, 0},
    {"cursor_attack_bd", "sm_cursor_attack_bd", 0, 0},
    {"cursor_attack_lh", "sm_cursor_attack_lh", 0, 0},
    {"cursor_attack_rh", "sm_cursor_attack_rh", 0, 0},
    {"cursor_attack_ll", "sm_cursor_attack_ll", 0, 0},
    {"cursor_attack_rl", "sm_cursor_attack_rl", 0, 0},
    {"cursor_camera", "sm_cursor_camera", 0, 0},
    {"cursor_spellcancel", "sm_cursor_spellcancel", 0, 0},
    {"cursor_steal", "sm_cursor_steal", 0, 0},
};
} // namespace

// Set-up. `modeTwo` picks the cursor's mode 2 (else 1); `context` and
// `smallCursors` (low byte tested) go to the cursor's Init, `flags` to every shape.
void SetUpCursors(bool modeTwo, CDirectInputMouse* context, int smallCursors, int flags) {
    g_cursor->Init(g_sceneRenderer->device_, modeTwo ? 2 : 1,
                                                context, smallCursors);
    bool useSmall = static_cast<std::uint8_t>(smallCursors) != 0;
    for (const CursorShape& shape : kShapes)
        g_cursor->AddShape(useSmall ? shape.smallName : shape.name, shape.hotX, shape.hotY, flags);
    g_cursor->Start();
}

void ResetCursors(bool modeTwo, CDirectInputMouse* context, int smallCursors, int flags) {
    g_cursor->Shutdown();   // the drawing thread stopped, the shapes released
    SetUpCursorsAt(modeTwo, context, smallCursors, flags);
}
