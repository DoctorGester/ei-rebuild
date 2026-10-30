// The base input side's double-click slots and the event-by-number call the dispatchers use.
// See ui.h (UiInputSide) and docs/notes/ui.md, section 1.
#include "ui.h"

void UiInputSide::LeftDouble(int buttons, int x, int y) { LeftDown(buttons, x, y); }

void UiInputSide::RightDouble(int buttons, int x, int y) { RightDown(buttons, x, y); }

void UiInputSide::Event(int slot, int first, int second, int third) {
    switch (slot) {
    case kEventDeactivate: Deactivate(); break;
    case kEventLeftDown: LeftDown(first, second, third); break;
    case kEventLeftUp: LeftUp(first, second, third); break;
    case kEventMouseMove: MouseMove(first, second, third); break;
    case kEventRightDown: RightDown(first, second, third); break;
    case kEventRightUp: RightUp(first, second, third); break;
    case kEventLeftDouble: LeftDouble(first, second, third); break;
    case kEventRightDouble: RightDouble(first, second, third); break;
    case kEventKeyDown: KeyDown(first, second, third); break;
    case kEventChar: Char(first, second, third); break;
    case kEventKeyUp: KeyUp(first, second, third); break;
    default: break;
    }
}

void UiInputSide::Wheel(int, int, int, int) {}
