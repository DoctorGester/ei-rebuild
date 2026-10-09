// Checks for ui_spin.cpp (A-H19). Scenarios: ui_hud, ui_main_menu and ui_container_input open the
// options window, whose Build makes the spins (0065BCD0, AddText) and sets their values; its
// Apply reads them (GetValue).
// - CUiSpin::Construct (0065BCD0): ours only (it allocates): vtable, one text, value 0, a surface.
// - CUiSpin::AddText (0065BE70): ours only: one more text, the given one (or empty).
// - CUiSpin::GetValue (0065BEB0): side by side (the original first, the changed flag put back).
// - CUiSpin::SetValue (0065BEE0): ours only (it draws): the value kept or 0, changed set.
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstring>

namespace {
void* s_unused;
} // namespace

static CUiSpin* __fastcall SpinConstructVerify(CUiSpin* self, void*, CInterface3D* ui, RECT rect, const char* text) {
    self->CUiSpin::Construct(ui, rect, text);
    bool ok = self->vtable_ == reinterpret_cast<void**>(std::uintptr_t(0x00742D5C)) && self->texts_.size == 1 &&
              self->value_ == 0 && self->changed_ && self->surfaces_.size == 1 &&
              std::memcmp(&self->layout_, &rect, sizeof(rect)) == 0 &&
              std::strcmp(self->texts_.data[0], text ? text : "") == 0;
    ei::verify::Check("CUiSpin::Construct", true, ok, "'%s'", text ? text : "(null)");
    return self;
}
EI_VERIFY(0x0065BCD0, SpinConstructVerify, 0, s_unused);

static void __fastcall SpinAddTextVerify(CUiSpin* self, void*, const char* text) {
    int size = self->texts_.size;
    self->CUiSpin::AddText(text);
    bool ok = self->texts_.size == size + 1 && std::strcmp(self->texts_.data[size], text ? text : "") == 0;
    ei::verify::Check("CUiSpin::AddText", true, ok, "'%s' %d", text ? text : "(null)", size);
}
EI_VERIFY(0x0065BE70, SpinAddTextVerify, 0, s_unused);

// 0065BEB0: MOV EDX,[ESP+4] (4); MOV EAX,[ECX+0x15C] (6).
static bool(__fastcall* s_getValue)(CUiSpin*, void*, int*);
static bool __fastcall SpinGetValueVerify(CUiSpin* self, void*, int* value) {
    bool changed = self->changed_;
    int expectedValue = 0;
    bool expected = s_getValue(self, nullptr, &expectedValue);
    bool expectedFlag = self->changed_;
    self->changed_ = changed;
    bool actual = self->CUiSpin::GetValue(value);
    bool ok = actual == expected && *value == expectedValue && self->changed_ == expectedFlag;
    ei::verify::Check("CUiSpin::GetValue", true, ok, "%d/%d changed %d", expectedValue, *value, changed);
    return actual;
}
EI_VERIFY(0x0065BEB0, SpinGetValueVerify, 10, s_getValue);

static void __fastcall SpinSetValueVerify(CUiSpin* self, void*, int value) {
    self->CUiSpin::SetValue(value);
    bool ok = self->value_ == (value < self->texts_.size ? value : 0) && self->changed_;
    ei::verify::Check("CUiSpin::SetValue", true, ok, "%d of %d -> %d", value, self->texts_.size, self->value_);
}
EI_VERIFY(0x0065BEE0, SpinSetValueVerify, 0, s_unused);
