// The mouse cursor's set-up (005DAEA0) and reset (005DAE70): the cursor object (007C7BD8) is
// initialised and given its 25 shapes, the "sm_" set when the small cursors are on. The cursor
// object itself (005E5xxx-005E6xxx) is not rewritten yet.
#include "ei/hook.h"

#include <cstdint>

namespace {
// The cursor object (007C7BD8) and its methods (thiscall).
void* CursorObject() { return reinterpret_cast<void*>(0x007C7BD8); }
// The window the cursor is drawn in (007AF080).
std::uint32_t CursorWindow() { return *reinterpret_cast<const std::uint32_t*>(0x007AF080); }
// 005E60B0 (RET 0x10): Init(window, mode 1 or 2, context, small); the small flag is the low byte.
__declspec(naked) void __fastcall CursorInit(void*, void*, std::uint32_t, int, int, int) EI_ORIGINAL(0x005E60B0)
// 005E63F0 (RET 0x10): adds the shape `name` with its hot spot (x, y).
__declspec(naked) void __fastcall CursorAddShape(void*, void*, const char*, int, int, int) EI_ORIGINAL(0x005E63F0)
// 005E6260: shows shape 0 and (unless mode 1) starts the drawing thread. 005E62E0: stops the
// thread and releases the shapes.
__declspec(naked) void __fastcall CursorStart(void*) EI_ORIGINAL(0x005E6260)
__declspec(naked) void __fastcall CursorShutdown(void*) EI_ORIGINAL(0x005E62E0)
// 005DAEA0 through its address (the verify build's wrapper sees the call from the reset).
__declspec(naked) void __fastcall SetUpCursorsAt(bool, int, int, int) EI_ORIGINAL(0x005DAEA0)

struct CursorShape {
    const char* name;
    const char* smallName;
    int hotX, hotY;
};
// In the shape order the game selects them by (005E5200's index).
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

// 005DAEA0 (fastcall, RET 8). `modeTwo` picks the cursor's mode 2 (else 1); `context` and
// `smallCursors` (low byte tested) go to the cursor's Init, `flags` to every shape.
void __fastcall SetUpCursors(bool modeTwo, int context, int smallCursors, int flags) {
    CursorInit(CursorObject(), nullptr, CursorWindow(), modeTwo ? 2 : 1, context, smallCursors);
    bool useSmall = static_cast<std::uint8_t>(smallCursors) != 0;
    for (const CursorShape& shape : kShapes)
        CursorAddShape(CursorObject(), nullptr, useSmall ? shape.smallName : shape.name, shape.hotX, shape.hotY, flags);
    CursorStart(CursorObject());
}

// 005DAE70 (fastcall, RET 8). Shuts the cursor down and sets it up again.
void __fastcall ResetCursors(bool modeTwo, int context, int smallCursors, int flags) {
    CursorShutdown(CursorObject());
    SetUpCursorsAt(modeTwo, context, smallCursors, flags);
}

EI_REPLACE(0x005DAEA0, SetUpCursors);
EI_REPLACE(0x005DAE70, ResetCursors);
