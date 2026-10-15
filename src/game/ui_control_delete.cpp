// Destructors of the interface controls (A-H20): the scroll bar, options slider, spin, edit field,
// scrolling text and tooltip (destructor bodies and slot 0, the scalar deleting destructors, with their input
// sides' copies: `this` moved back from the input side at +0x94, then the window's), and the
// scrolling text's tokens. The scrolling text's body is in ui_scroll_text.cpp. See ui.h and
// docs/notes/items_spells.md.
#include "ui.h"
#include "ui_screens.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>

namespace {
constexpr std::uintptr_t kScrollbarVtable = std::uintptr_t(0x00742C18);
constexpr std::uintptr_t kScrollbarInputVtable = std::uintptr_t(0x00742BE4);
constexpr std::uintptr_t kSpinVtable = std::uintptr_t(0x00742D5C);
constexpr std::uintptr_t kSpinInputVtable = std::uintptr_t(0x00742D28);
constexpr std::uintptr_t kEditFieldVtable = std::uintptr_t(0x00742DF4);
constexpr std::uintptr_t kEditFieldInputVtable = std::uintptr_t(0x00742DC0);

template <typename T>
T* FreeIfAsked(T* self, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
} // namespace

// --- Bodies -------------------------------------------------------------------------------------

void CUiScrollbar::DestroyBody() {
    vtable_ = reinterpret_cast<void**>(kScrollbarVtable);
    inputVtable_ = reinterpret_cast<void**>(kScrollbarInputVtable);
    DestroyBase();
}
EI_REPLACE_METHOD(0x0065AFC0, CUiScrollbar::DestroyBody);
EI_REPLACE_METHOD(0x0065BCB0, CUiScrollbar::DestroyBody);   // the slider's (the same code)

void CUiSpin::DestroyBody() {
    vtable_ = reinterpret_cast<void**>(kSpinVtable);
    inputVtable_ = reinterpret_cast<void**>(kSpinInputVtable);
    texts_.Destruct();
    DestroyBase();
}
EI_REPLACE_METHOD(0x0065BE10, CUiSpin::DestroyBody);

void CUiEditField::DestroyBody() {
    vtable_ = reinterpret_cast<void**>(kEditFieldVtable);
    inputVtable_ = reinterpret_cast<void**>(kEditFieldInputVtable);
    Text().~CString();
    DestroyBase();
}
EI_REPLACE_METHOD(0x0065C140, CUiEditField::DestroyBody);

// --- Deleting destructors (slot 0) ----------------------------------------------------------------

// 0065AFA0 / 0065BC90: the scroll bar (00742C18) and the slider (00742CBC).
CUiScrollbar* CUiScrollbar::ScrollbarDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0065AFA0, CUiScrollbar::ScrollbarDelete);
EI_REPLACE_METHOD(0x0065BC90, CUiScrollbar::ScrollbarDelete);

// 0065BDF0: the spin (00742D5C).
CUiSpin* CUiSpin::SpinDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0065BDF0, CUiSpin::SpinDelete);

// 0065C120: the edit field (00742DF4).
CUiEditField* CUiEditField::EditFieldDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0065C120, CUiEditField::EditFieldDelete);

// 0065CE00: the scrolling text (00742E9C).
CUiScrollText* CUiScrollText::ScrollTextDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0065CE00, CUiScrollText::ScrollTextDelete);

// 0065E4D0: a region's tooltip (00742F64).
CUiTooltip* CUiTooltip::TooltipDelete(unsigned flags) {
    DestroyBody();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0065E4D0, CUiTooltip::TooltipDelete);

// The input sides' slot 0 (SUB ECX,0x94; JMP to the window's): 0065CB70 the scroll bar's, 0065CB80
// the slider's, 0065CB90 the spin's, 0065CBA0 the edit field's, 0065E2B0 the scrolling text's,
// 0065EAC0 the tooltip's.
EI_REPLACE_METHOD(0x0065CB70, UiInput::DeleteWindow<&CUiScrollbar::ScrollbarDelete>);
EI_REPLACE_METHOD(0x0065CB80, UiInput::DeleteWindow<&CUiScrollbar::ScrollbarDelete>);
EI_REPLACE_METHOD(0x0065CB90, UiInput::DeleteWindow<&CUiSpin::SpinDelete>);
EI_REPLACE_METHOD(0x0065CBA0, UiInput::DeleteWindow<&CUiEditField::EditFieldDelete>);
EI_REPLACE_METHOD(0x0065E2B0, UiInput::DeleteWindow<&CUiScrollText::ScrollTextDelete>);
EI_REPLACE_METHOD(0x0065EAC0, UiInput::DeleteWindow<&CUiTooltip::TooltipDelete>);

// --- The scrolling text's tokens ------------------------------------------------------------------

// 0065D8A0: the base token (00742F24; its body inlined: the base vtable written).
UiTextToken* UiTextToken::TextTokenDelete(unsigned flags) {
    ei::SetVtable(this, UiTextToken::kVtableAddress);
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0065D8A0, UiTextToken::TextTokenDelete);

// 0065D8C0: the format token (00742F28).
UiTextToken* UiTextToken::FormatTokenDelete(unsigned flags) {
    DestroyBase();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0065D8C0, UiTextToken::FormatTokenDelete);

// 0065D8F0: the word (00742F20).
UiWordToken* UiWordToken::WordTokenDelete(unsigned flags) {
    DestroyWord();
    return FreeIfAsked(this, flags);
}
EI_REPLACE_METHOD(0x0065D8F0, UiWordToken::WordTokenDelete);
