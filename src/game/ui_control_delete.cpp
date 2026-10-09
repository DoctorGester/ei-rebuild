// Destructors of the interface controls (A-H20): the scroll bar, options slider, spin, edit field
// and scrolling text (destructor bodies and slot 0, the scalar deleting destructors, with their input
// sides' copies: `this` moved back from the input side at +0x94, then the window's), and the
// scrolling text's tokens. The scrolling text's body is in ui_scroll_text.cpp. See ui.h and
// docs/notes/items_spells.md.
#include "ui.h"
#include "ui_screens.h"

#include "ei/hook.h"
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
constexpr std::uintptr_t kTokenVtable = std::uintptr_t(0x00742F24);   // the tokens' base

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
CUiScrollbar* __fastcall ScrollbarDelete(CUiScrollbar* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0065AFA0, ScrollbarDelete);
EI_REPLACE(0x0065BC90, ScrollbarDelete);

// 0065BDF0: the spin (00742D5C).
CUiSpin* __fastcall SpinDelete(CUiSpin* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0065BDF0, SpinDelete);

// 0065C120: the edit field (00742DF4).
CUiEditField* __fastcall EditFieldDelete(CUiEditField* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0065C120, EditFieldDelete);

// 0065CE00: the scrolling text (00742E9C).
CUiScrollText* __fastcall ScrollTextDelete(CUiScrollText* self, void*, unsigned flags) {
    self->DestroyBody();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0065CE00, ScrollTextDelete);

// The input sides' slot 0 (SUB ECX,0x94; JMP to the window's): 0065CB70 the scroll bar's, 0065CB80
// the slider's, 0065CB90 the spin's, 0065CBA0 the edit field's, 0065E2B0 the scrolling text's.
void* __fastcall ScrollbarInputDelete(void* input, void*, unsigned flags) {
    return ScrollbarDelete(WindowFromInput<CUiScrollbar>(input), nullptr, flags);
}
EI_REPLACE(0x0065CB70, ScrollbarInputDelete);
EI_REPLACE(0x0065CB80, ScrollbarInputDelete);
void* __fastcall SpinInputDelete(void* input, void*, unsigned flags) {
    return SpinDelete(WindowFromInput<CUiSpin>(input), nullptr, flags);
}
EI_REPLACE(0x0065CB90, SpinInputDelete);
void* __fastcall EditFieldInputDelete(void* input, void*, unsigned flags) {
    return EditFieldDelete(WindowFromInput<CUiEditField>(input), nullptr, flags);
}
EI_REPLACE(0x0065CBA0, EditFieldInputDelete);
void* __fastcall ScrollTextInputDelete(void* input, void*, unsigned flags) {
    return ScrollTextDelete(WindowFromInput<CUiScrollText>(input), nullptr, flags);
}
EI_REPLACE(0x0065E2B0, ScrollTextInputDelete);

// --- The scrolling text's tokens ------------------------------------------------------------------

// 0065D8A0: the base token (00742F24; its body inlined: the base vtable written).
UiTextToken* __fastcall TextTokenDelete(UiTextToken* self, void*, unsigned flags) {
    self->vtable_ = reinterpret_cast<void*>(kTokenVtable);
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0065D8A0, TextTokenDelete);

// 0065D8C0: the format token (00742F28).
UiTextToken* __fastcall FormatTokenDelete(UiTextToken* self, void*, unsigned flags) {
    self->DestroyBase();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0065D8C0, FormatTokenDelete);

// 0065D8F0: the word (00742F20).
UiWordToken* __fastcall WordTokenDelete(UiWordToken* self, void*, unsigned flags) {
    self->DestroyWord();
    return FreeIfAsked(self, flags);
}
EI_REPLACE(0x0065D8F0, WordTokenDelete);
