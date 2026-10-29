// Destructors of the interface controls: the scroll bar and options slider, the spin and the edit field (the
// scrolling text's and the tooltip's are in ui_scroll_text.cpp / ui_tooltip.cpp). See ui.h and
// docs/notes/items_spells.md.
#include "ui.h"
#include "ui_screens.h"

#include <cstdint>

// --- The controls' destructors (with their deleting destructors and the input sides' copies) --

UiScrollbar::~UiScrollbar() {}

UiSpin::~UiSpin() = default;

UiEditField::~UiEditField() {}
