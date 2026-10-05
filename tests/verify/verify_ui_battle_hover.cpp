// CBattleScreen::HoverUnit (00639EF0, ui_battle_hover.cpp) runs side by side. Its effects are the
// cursor shape (the cursor's +0x18) and the unit panel's hover block (+0x150, with the panel
// refresh 0063B920 when the unit changes); everything else it calls only asks (picking, exits,
// variables, CanCastSpell / CanUseQuickItem). Ours runs first (doing the panel refresh), the
// cursor is put back to its shape before the call, then the original runs over the same state
// (its panel unit is already ours, so it refreshes nothing); the shape and the panel unit after
// each run must agree. Reached from the battle screen's mouse move (00639D60) in every scenario
// with a game and a `move`, and from the portraits' mouse move (ours calls it directly, unwrapped).
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstdint>
#include <cstring>

namespace {

int CursorShape() { return *reinterpret_cast<const int*>(0x007C7BD8 + 0x18); }
void SetCursorShape(int shape) {
    using Fn = int(__thiscall*)(void*, int);
    reinterpret_cast<Fn>(0x005E5200)(reinterpret_cast<void*>(0x007C7BD8), shape);
}
void* PanelUnit(CBattleScreen* screen) {
    std::uint8_t* panel;
    std::memcpy(&panel, reinterpret_cast<std::uint8_t*>(screen) + 0x1A8, 4);
    void* unit;
    std::memcpy(&unit, panel + 0x150, 4);
    return unit;
}

} // namespace

// 00639EF0: MOV EAX,FS:[0] (6).
static void(__fastcall* s_hoverUnit)(CBattleScreen*, void*, CUnit*, int, int);
static void __fastcall HoverUnitVerify(CBattleScreen* self, void*, CUnit* unit, int x, int y) {
    int before = CursorShape();
    self->HoverUnit(unit, x, y);
    int ourShape = CursorShape();
    void* ourUnit = PanelUnit(self);
    if (before != -1) SetCursorShape(before);
    s_hoverUnit(self, nullptr, unit, x, y);
    int shape = CursorShape();
    void* panelUnit = PanelUnit(self);
    ei::verify::Check("CBattleScreen::HoverUnit", shape, ourShape, "cursor %d -> %d, ours %d (action %d, (%d,%d))", before,
                      shape, ourShape, *reinterpret_cast<const int*>(reinterpret_cast<std::uint8_t*>(self) + 0x17C), x, y);
    ei::verify::Check("CBattleScreen::HoverUnit", panelUnit, ourUnit, "panel unit %p, ours %p", panelUnit, ourUnit);
}
EI_VERIFY(0x00639EF0, HoverUnitVerify, 6, s_hoverUnit);
