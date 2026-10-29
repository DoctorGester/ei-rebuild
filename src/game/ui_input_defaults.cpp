// The base input side's double-click slots and the event-by-number call the dispatchers use.
// See ui.h (UiInputSide) and docs/notes/ui.md, section 1.
#include "ui.h"

void UiInputSide::LeftDouble(int buttons, int x, int y) { LeftDown(buttons, x, y); }

void UiInputSide::RightDouble(int buttons, int x, int y) { RightDown(buttons, x, y); }

void UiInputSide::Event(int slot, int a, int b, int c) {
    switch (slot) {
    case kEventDeactivate: Deactivate(); break;
    case kEventLeftDown: LeftDown(a, b, c); break;
    case kEventLeftUp: LeftUp(a, b, c); break;
    case kEventMouseMove: MouseMove(a, b, c); break;
    case kEventRightDown: RightDown(a, b, c); break;
    case kEventRightUp: RightUp(a, b, c); break;
    case kEventLeftDouble: LeftDouble(a, b, c); break;
    case kEventRightDouble: RightDouble(a, b, c); break;
    case kEventKeyDown: KeyDown(a, b, c); break;
    case kEventChar: Char(a, b, c); break;
    case kEventKeyUp: KeyUp(a, b, c); break;
    default: break;
    }
}
