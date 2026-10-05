// Checks for the battle screen's mouse move (00639D60) and a unit's name line (0063B920)
// (ui_battle_mouse.cpp). Both run side by side with the original.
#include "verify.h"

#include "../../src/game/ui_camp.h"
#include "../../src/game/ui_cursor.h"
#include "../../src/game/unit.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace {

// What the mouse move changes: the cursor shape, the screen's handled flag (a panel took the
// move), the selection box (+0x1C1, +0x1CC) and the unit panel's hover unit.
struct MoveState {
    int shape;
    std::uint32_t handled;
    std::uint32_t dragging;
    RECT box;
    CUnit* hover;
};

CHudUnitPanel* UnitPanel(CBattleScreenSetup* self) { return static_cast<CHudUnitPanel*>(self->unitPanel_); }

MoveState Snapshot(CBattleScreenSetup* self) {
    return MoveState{GameCursor()->shape_, self->inputHandled_ ? 1u : 0u, self->unknown_1C1 ? 1u : 0u,
                     self->selectionBox_, self->unitPanel_ ? UnitPanel(self)->hoverUnit_ : nullptr};
}

void Restore(CBattleScreenSetup* self, const MoveState& s) {
    if (s.shape != -1) GameCursor()->SetShape(s.shape);
    self->inputHandled_ = s.handled != 0;
    self->unknown_1C1 = s.dragging != 0;
    self->selectionBox_ = s.box;
    if (self->unitPanel_) UnitPanel(self)->hoverUnit_ = s.hover;
}

} // namespace

// CBattleScreenSetup::OnMouseMove (00639D60): the original first, its effects recorded and the
// state before put back (the cursor through SetShape), then ours; the cursor shape, handled flag,
// box and hover unit must agree. Everything else it reaches runs twice, as when the battle
// screen's Draw repeats the move at the same point every frame: the panels' own mouse moves
// (006012D0, the portraits' in a player pick) and HoverUnit (whose wrapper checks it). Reached
// every frame in every scenario with a game (Draw), and by every `move` / `click` on the view.
// 00639D60: PUSH EBX; PUSH ESI; MOV ESI,ECX; PUSH EDI (5).
static void(__fastcall* s_mouseMove)(void*, void*, int, int, int);
static void __fastcall BattleMouseMoveVerify(void* input, void*, int buttons, int x, int y) {
    auto* self = WindowFromInput<CBattleScreenSetup>(input);
    MoveState before = Snapshot(self);
    s_mouseMove(input, nullptr, buttons, x, y);
    MoveState expected = Snapshot(self);
    Restore(self, before);
    self->CBattleScreenSetup::OnMouseMove(buttons, x, y);
    MoveState actual = Snapshot(self);
    ei::verify::Check("CBattleScreenSetup::OnMouseMove", expected, actual,
                      "(%d,%d) press %d drag %u->%u/%u shape %d->%d/%d handled %u/%u hover %p/%p box %d,%d-%d,%d / "
                      "%d,%d-%d,%d",
                      x, y, self->unknown_1C0 ? 1 : 0, before.dragging, expected.dragging, actual.dragging,
                      before.shape, expected.shape, actual.shape, expected.handled, actual.handled, expected.hover,
                      actual.hover, expected.box.left, expected.box.top, expected.box.right, expected.box.bottom,
                      actual.box.left, actual.box.top, actual.box.right, actual.box.bottom);
}
EI_VERIFY(0x00639D60, BattleMouseMoveVerify, 5, s_mouseMove);

// CBattleScreenSetup::UnitName (0063B920): the original into a scratch string, ours into the
// caller's; the texts must match. Both read only (the text manager's cursor ends where ours
// leaves it). Reached by the selection refresh (00637DC0: every game scenario's new hero),
// HoverUnit (a unit under the mouse) and the speech lines (006366F0).
// 0063B920: PUSH -1; PUSH 0x0072DE48 (7).
static void(__fastcall* s_unitName)(CBattleScreenSetup*, void*, CUnit*, mfc::CString*);
static void __fastcall UnitNameVerify(CBattleScreenSetup* self, void*, CUnit* unit, mfc::CString* out) {
    mfc::CString expected;
    s_unitName(self, nullptr, unit, &expected);
    self->CBattleScreenSetup::UnitName(unit, out);
    const char* mine = *out;
    ei::verify::Check("CBattleScreenSetup::UnitName", true, std::strcmp(expected, mine) == 0,
                      "unit %d: \"%.80s\" / \"%.80s\"", unit->id_, static_cast<const char*>(expected), mine);
}
EI_VERIFY(0x0063B920, UnitNameVerify, 7, s_unitName);
