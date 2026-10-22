// Destructors of the interface controls (A-H20): the scroll bar and options slider, the spin and the edit field
// (the scrolling text's and the tooltip's are in ui_scroll_text.cpp / ui_tooltip.cpp), and the scrolling text's
// tokens. See ui.h and docs/notes/items_spells.md.
#include "ui.h"
#include "ui_screens.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "game_data.h"

#include <cstdint>

namespace {
template <typename T>
T* FreeIfAsked(T* self, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
} // namespace

// --- The controls' destructors (with their deleting destructors and the input sides' copies: SUB ECX,0x94; JMP) --

CUiScrollbar::~CUiScrollbar() {}
EI_REWRITTEN_SPECIAL(0x0065AFC0, CUiScrollbar::~CUiScrollbar);
EI_REWRITTEN_SPECIAL(0x0065BCB0, CUiScrollbar::~CUiScrollbar);   // the slider's
EI_REWRITTEN_SPECIAL(0x0065AFA0, CUiScrollbar::~CUiScrollbar);   // deleting
EI_REWRITTEN_SPECIAL(0x0065BC90, CUiScrollbar::~CUiScrollbar);   // the slider's deleting
EI_REWRITTEN_SPECIAL(0x0065CB70, CUiScrollbar::~CUiScrollbar);   // input side
EI_REWRITTEN_SPECIAL(0x0065CB80, CUiScrollbar::~CUiScrollbar);   // the slider's input side

CUiSpin::~CUiSpin() { texts_.Destruct(); }
EI_REWRITTEN_SPECIAL(0x0065BE10, CUiSpin::~CUiSpin);
EI_REWRITTEN_SPECIAL(0x0065BDF0, CUiSpin::~CUiSpin);   // deleting
EI_REWRITTEN_SPECIAL(0x0065CB90, CUiSpin::~CUiSpin);   // input side

CUiEditField::~CUiEditField() {}
EI_REWRITTEN_SPECIAL(0x0065C140, CUiEditField::~CUiEditField);
EI_REWRITTEN_SPECIAL(0x0065C120, CUiEditField::~CUiEditField);   // deleting
EI_REWRITTEN_SPECIAL(0x0065CBA0, CUiEditField::~CUiEditField);   // input side

// --- The scrolling text's tokens ------------------------------------------------------------------

// 0065D8A0: the base token (TextTokenClass; its body inlined: the base vtable written).
UiTextToken* UiTextToken::TextTokenDelete(unsigned flags) {
    ei::SetVtable(this, TextTokenClass::Vtable());
    return FreeIfAsked(this, flags);
}
EI_REWRITTEN(0x0065D8A0, UiTextToken::TextTokenDelete);

// 0065D8C0: the format token (FormatTokenClass).
UiTextToken* UiTextToken::FormatTokenDelete(unsigned flags) {
    DestroyBase();
    return FreeIfAsked(this, flags);
}
EI_REWRITTEN(0x0065D8C0, UiTextToken::FormatTokenDelete);

// 0065D8F0: the word (WordTokenClass).
UiWordToken* UiWordToken::WordTokenDelete(unsigned flags) {
    DestroyWord();
    return FreeIfAsked(this, flags);
}
EI_REWRITTEN(0x0065D8F0, UiWordToken::WordTokenDelete);
