// The base input side's double-click slots (005E2860 / 005E2880). See ui.h (UiInputSide) and
// docs/notes/ui.md, section 1.
#include "ui.h"

#include "ei/hook.h"

int UiInputSide::LeftDoubleAsDown(int buttons, int x, int y) { return Event(kLeftDown, buttons, x, y); }
EI_REPLACE_METHOD(0x005E2860, UiInputSide::LeftDoubleAsDown);

int UiInputSide::RightDoubleAsDown(int buttons, int x, int y) { return Event(kRightDown, buttons, x, y); }
EI_REPLACE_METHOD(0x005E2880, UiInputSide::RightDoubleAsDown);
