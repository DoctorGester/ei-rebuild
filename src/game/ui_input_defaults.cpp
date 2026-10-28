// The base input side's double-click slots and the event-by-number call the dispatchers use.
// See ui.h (UiInputSide) and docs/notes/ui.md, section 1.
#include "ui.h"

int UiInputSide::LeftDouble(int buttons, int x, int y) { return LeftDown(buttons, x, y); }

int UiInputSide::RightDouble(int buttons, int x, int y) { return RightDown(buttons, x, y); }

int UiInputSide::Event(int slot, int a, int b, int c) {
    switch (slot) {
    case kEventDeactivate: Deactivate(); return 0;
    case kEventLeftDown: return LeftDown(a, b, c);
    case kEventLeftUp: return LeftUp(a, b, c);
    case kEventMouseMove: return MouseMove(a, b, c);
    case kEventRightDown: return RightDown(a, b, c);
    case kEventRightUp: return RightUp(a, b, c);
    case kEventLeftDouble: return LeftDouble(a, b, c);
    case kEventRightDouble: return RightDouble(a, b, c);
    case kEventKeyDown: return KeyDown(a, b, c);
    case kEventChar: return Char(a, b, c);
    case kEventKeyUp: return KeyUp(a, b, c);
    default: return 0;
    }
}
