// Checks for the interface controls' destructors (ui_control_delete.cpp, A-H20). All ours only (they
// free memory): a deleting destructor returns the object (input sides: the window, 0x94 before the
// side); after a body the window base holds nothing; a token's body leaves the base vtable.
// Scenarios: every scenario closes the message log at exit; ui_objectives, ui_saveload and
// ui_options close their scroll bars, sliders and spins; the tokens go whenever a log text is
// replaced (SetText, Clear) or the log is deleted. The edit field: only the multiplayer screens and
// message boxes with an input line (no scenario).
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstdint>

CUiScrollbar* __fastcall ScrollbarDelete(CUiScrollbar* self, void*, unsigned flags);
CUiSpin* __fastcall SpinDelete(CUiSpin* self, void*, unsigned flags);
CUiEditField* __fastcall EditFieldDelete(CUiEditField* self, void*, unsigned flags);
CUiScrollText* __fastcall ScrollTextDelete(CUiScrollText* self, void*, unsigned flags);
void* __fastcall ScrollbarInputDelete(void* input, void*, unsigned flags);
void* __fastcall SpinInputDelete(void* input, void*, unsigned flags);
void* __fastcall EditFieldInputDelete(void* input, void*, unsigned flags);
void* __fastcall ScrollTextInputDelete(void* input, void*, unsigned flags);
UiTextToken* __fastcall TextTokenDelete(UiTextToken* self, void*, unsigned flags);
UiTextToken* __fastcall FormatTokenDelete(UiTextToken* self, void*, unsigned flags);
UiWordToken* __fastcall WordTokenDelete(UiWordToken* self, void*, unsigned flags);

namespace {
void* s_unused;

bool BaseEmpty(const CUiWindow* w) {
    return !w->blackMaterial_ && !w->whiteMaterial_ && !w->unknown_0F4 && !w->objects_.data && !w->sceneObjects_.data &&
           !w->surfaces_.data && !w->regions_.data && !w->models_.data;
}
void CheckDeleted(const char* name, const void* expected, const void* result, unsigned flags) {
    ei::verify::Check(name, true, result == expected, "%p -> %p flags %u", expected, result, flags);
}
const void* WindowOf(const void* input) { return static_cast<const char*>(input) - 0x94; }
bool BaseTokenVtable(const UiTextToken* token) {
    return token->vtable_ == reinterpret_cast<void*>(std::uintptr_t(0x00742F24));
}
} // namespace

static void __fastcall ScrollbarDestroyVerify(CUiScrollbar* self) {
    self->CUiScrollbar::DestroyBody();
    ei::verify::Check("CUiScrollbar::DestroyBody", true, BaseEmpty(self), "");
}
EI_VERIFY(0x0065AFC0, ScrollbarDestroyVerify, 0, s_unused);
EI_VERIFY(0x0065BCB0, ScrollbarDestroyVerify, 0, s_unused);

static void __fastcall SpinDestroyVerify(CUiSpin* self) {
    self->CUiSpin::DestroyBody();
    ei::verify::Check("CUiSpin::DestroyBody", true, BaseEmpty(self), "");
}
EI_VERIFY(0x0065BE10, SpinDestroyVerify, 0, s_unused);

static void __fastcall EditFieldDestroyVerify(CUiEditField* self) {
    self->CUiEditField::DestroyBody();
    ei::verify::Check("CUiEditField::DestroyBody", true, BaseEmpty(self), "");
}
EI_VERIFY(0x0065C140, EditFieldDestroyVerify, 0, s_unused);

static void __fastcall TokenDestroyVerify(UiTextToken* self) {
    self->UiTextToken::DestroyBase();
    ei::verify::Check("UiTextToken::DestroyBase", true, BaseTokenVtable(self), "");
}
EI_VERIFY(0x0065D8E0, TokenDestroyVerify, 0, s_unused);
EI_VERIFY(0x0065D960, TokenDestroyVerify, 0, s_unused);

static void __fastcall WordDestroyVerify(UiWordToken* self) {
    self->UiWordToken::DestroyWord();
    ei::verify::Check("UiWordToken::DestroyWord", true, BaseTokenVtable(self), "");
}
EI_VERIFY(0x0065D910, WordDestroyVerify, 0, s_unused);

#define EI_DELETE_VERIFY(addr, Wrapper, Fn, T)                                       \
    static void* __fastcall Wrapper(T* self, void*, unsigned flags) {                 \
        void* result = Fn(self, nullptr, flags);                                     \
        CheckDeleted(#Fn, self, result, flags);                                      \
        return result;                                                               \
    }                                                                                \
    EI_VERIFY(addr, Wrapper, 0, s_unused);

EI_DELETE_VERIFY(0x0065AFA0, ScrollbarDeleteVerify, ScrollbarDelete, CUiScrollbar)
EI_DELETE_VERIFY(0x0065BC90, SliderDeleteVerify, ScrollbarDelete, CUiScrollbar)
EI_DELETE_VERIFY(0x0065BDF0, SpinDeleteVerify, SpinDelete, CUiSpin)
EI_DELETE_VERIFY(0x0065C120, EditFieldDeleteVerify, EditFieldDelete, CUiEditField)
EI_DELETE_VERIFY(0x0065CE00, ScrollTextDeleteVerify, ScrollTextDelete, CUiScrollText)
EI_DELETE_VERIFY(0x0065D8A0, TextTokenDeleteVerify, TextTokenDelete, UiTextToken)
EI_DELETE_VERIFY(0x0065D8C0, FormatTokenDeleteVerify, FormatTokenDelete, UiTextToken)
EI_DELETE_VERIFY(0x0065D8F0, WordTokenDeleteVerify, WordTokenDelete, UiWordToken)

#define EI_INPUT_DELETE_VERIFY(addr, Wrapper, Fn)                                    \
    static void* __fastcall Wrapper(void* input, void*, unsigned flags) {             \
        const void* window = WindowOf(input);                                        \
        void* result = Fn(input, nullptr, flags);                                    \
        CheckDeleted(#Fn, window, result, flags);                                    \
        return result;                                                               \
    }                                                                                \
    EI_VERIFY(addr, Wrapper, 0, s_unused);

EI_INPUT_DELETE_VERIFY(0x0065CB70, ScrollbarInputDeleteVerify, ScrollbarInputDelete)
EI_INPUT_DELETE_VERIFY(0x0065CB80, SliderInputDeleteVerify, ScrollbarInputDelete)
EI_INPUT_DELETE_VERIFY(0x0065CB90, SpinInputDeleteVerify, SpinInputDelete)
EI_INPUT_DELETE_VERIFY(0x0065CBA0, EditFieldInputDeleteVerify, EditFieldInputDelete)
EI_INPUT_DELETE_VERIFY(0x0065E2B0, ScrollTextInputDeleteVerify, ScrollTextInputDelete)
