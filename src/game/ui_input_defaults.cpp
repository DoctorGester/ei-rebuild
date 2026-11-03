// The base input side's double-click slots and the event-by-number call the dispatchers use.
// See ui.h (UiInputSide) and docs/notes/ui.md, section 1.
#include "ui.h"

void UiInputSide::LeftDouble(int buttons, Point point) { LeftDown(buttons, point); }

void UiInputSide::RightDouble(int buttons, Point point) { RightDown(buttons, point); }

void UiInputSide::Event(int slot, int first, int second, int third) {
    switch (slot) {
    case kEventDeactivate: Deactivate(); break;
    case kEventLeftDown: LeftDown(first, Point{second, third}); break;
    case kEventLeftUp: LeftUp(first, Point{second, third}); break;
    case kEventMouseMove: MouseMove(first, Point{second, third}); break;
    case kEventRightDown: RightDown(first, Point{second, third}); break;
    case kEventRightUp: RightUp(first, Point{second, third}); break;
    case kEventLeftDouble: LeftDouble(first, Point{second, third}); break;
    case kEventRightDouble: RightDouble(first, Point{second, third}); break;
    case kEventKeyDown: KeyDown(first, second, third); break;
    case kEventChar: Char(first, second, third); break;
    case kEventKeyUp: KeyUp(first, second, third); break;
    default: break;
    }
}

void UiInputSide::Wheel(int, int, Point) {}
