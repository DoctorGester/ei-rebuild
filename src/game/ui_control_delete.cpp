// Destructors of the interface controls: the scroll bar and options slider, the spin and the edit field (the
// scrolling text's and the tooltip's are in ui_scroll_text.cpp / ui_tooltip.cpp). See ui.h and
// docs/notes/items_spells.md.
#include "ui.h"
#include "ui_screens.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "game_data.h"

#include <cstdint>

// --- The controls' destructors (with their deleting destructors and the input sides' copies) --

CUiScrollbar::~CUiScrollbar() {}
EI_REWRITTEN_SPECIAL(0x0065AFC0, CUiScrollbar::~CUiScrollbar);
EI_REWRITTEN_SPECIAL(0x0065BCB0, CUiScrollbar::~CUiScrollbar);   // the slider's
EI_REWRITTEN_SPECIAL(0x0065AFA0, CUiScrollbar::~CUiScrollbar);
EI_REWRITTEN_SPECIAL(0x0065BC90, CUiScrollbar::~CUiScrollbar);   // the slider's deleting
EI_REWRITTEN_SPECIAL(0x0065CB70, CUiScrollbar::~CUiScrollbar);
EI_REWRITTEN_SPECIAL(0x0065CB80, CUiScrollbar::~CUiScrollbar);   // the slider's input side

CUiSpin::~CUiSpin() = default;
EI_REWRITTEN_SPECIAL(0x0065BE10, CUiSpin::~CUiSpin);
EI_REWRITTEN_SPECIAL(0x0065BDF0, CUiSpin::~CUiSpin);
EI_REWRITTEN_SPECIAL(0x0065CB90, CUiSpin::~CUiSpin);

CUiEditField::~CUiEditField() {}
EI_REWRITTEN_SPECIAL(0x0065C140, CUiEditField::~CUiEditField);
EI_REWRITTEN_SPECIAL(0x0065C120, CUiEditField::~CUiEditField);
EI_REWRITTEN_SPECIAL(0x0065CBA0, CUiEditField::~CUiEditField);

