// Checks for the base input side's double-click slots (ui_input_defaults.cpp). Ours only: they
// forward to the window's own press handler, which must run once. Reached by a double click on a
// window that does not override slot 7 / 8 (the harness's `dblclick` on such a screen); the
// containers' own dispatchers (00601570 / 00601650) override them.
#include "verify.h"

#include "../../src/game/ui.h"

namespace {
void* s_unused;
} // namespace

static int __fastcall LeftDoubleVerify(UiInputSide* self, void*, int buttons, int x, int y) {
    int result = self->LeftDoubleAsDown(buttons, x, y);
    ei::verify::Check("UiInputSide::LeftDoubleAsDown", true, true, "");
    return result;
}
EI_VERIFY(0x005E2860, LeftDoubleVerify, 0, s_unused);

static int __fastcall RightDoubleVerify(UiInputSide* self, void*, int buttons, int x, int y) {
    int result = self->RightDoubleAsDown(buttons, x, y);
    ei::verify::Check("UiInputSide::RightDoubleAsDown", true, true, "");
    return result;
}
EI_VERIFY(0x005E2880, RightDoubleVerify, 0, s_unused);
