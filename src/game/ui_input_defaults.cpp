// The base input side's double-click slots and the event-by-number call the dispatchers use.
// See ui.h (UiInputSide) and docs/notes/ui.md, section 1.
#include "ui.h"

void UiInputSide::LeftDouble(int buttons, Point point) { LeftDown(buttons, point); }

void UiInputSide::RightDouble(int buttons, Point point) { RightDown(buttons, point); }

void UiInputSide::Wheel(int, int, Point) {}
