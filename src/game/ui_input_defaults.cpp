// The base input side's double-click slots and the event-by-number call the dispatchers use.
// See ui.h (UiInputSide) and docs/notes/ui.md, section 1.
#include "ui.h"

void UiInputSide::LeftDouble(Point point) { LeftDown(point); }

void UiInputSide::RightDouble(Point point) { RightDown(point); }

void UiInputSide::Wheel(float, Point) {}
