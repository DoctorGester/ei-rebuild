// Checks for input_mouse.cpp (A-H25): the DirectInput mouse.
// - Setup (the application's start: every run) and Shutdown (its end) create and release COM
//   objects, and Poll (the cursor's motion, every frame the cursor is drawn by the game) consumes
//   the device's relative motion: ours only, with what must hold afterwards.
// - SetActive (the window gaining or losing the focus) only acquires or unacquires, which may be
//   repeated: the original, then ours, the low bytes of the results compared.
// - The mouse data format: a start-up check compares ours with the game's c_dfDIMouse (00745D00)
//   and its object table, field by field.
#include "verify.h"

#include "../../src/game/ui_cursor.h"

#include <cstring>

namespace {
void* s_unused;

int __fastcall SetupVerify(CDirectInputMouse* self, void*, HWND window, HINSTANCE instance) {
    int result = self->Setup(window, instance);
    const bool ok = (result & 0xFF) == 0 || (self->input_ != nullptr && self->device_ != nullptr);
    ei::verify::Check("CDirectInputMouse::Setup", true, ok, "mouse %p -> %d", static_cast<void*>(self), result);
    return result;
}

void __fastcall ShutdownVerify(CDirectInputMouse* self) {
    self->Shutdown();
    ei::verify::Check("CDirectInputMouse::Shutdown", true, self->input_ == nullptr && self->device_ == nullptr, "mouse %p",
                      static_cast<void*>(self));
}

// 0065EC70: MOV EAX,[ECX+0xC]; TEST EAX,EAX (5).
int(__fastcall* s_setActive)(CDirectInputMouse*, void*, char);
int __fastcall SetActiveVerify(CDirectInputMouse* self, void*, char active) {
    int expected = s_setActive(self, nullptr, active);
    int actual = self->SetActive(active);
    ei::verify::Check("CDirectInputMouse::SetActive", expected & 0xFF, actual & 0xFF, "active %d", active);
    return expected;
}

const LONG* __fastcall PollVerify(CDirectInputMouse* self, void*, int unused) {
    const LONG* state = self->Poll(unused);
    ei::verify::Check("CDirectInputMouse::Poll", true, state == self->state_, "mouse %p", static_cast<void*>(self));
    return state;
}
} // namespace

EI_VERIFY(0x0065EAD0, SetupVerify, 0, s_unused);
EI_VERIFY(0x0065EC30, ShutdownVerify, 0, s_unused);
EI_VERIFY(0x0065EC70, SetActiveVerify, 5, s_setActive);
EI_VERIFY(0x0065ECB0, PollVerify, 0, s_unused);

namespace {
void MouseFormatStartupCheck() {
    // DIDATAFORMAT: size, object size, flags, data size, object count, objects; each object: GUID
    // pointer, offset, type, flags.
    const auto* ours = static_cast<const std::uint32_t*>(CDirectInputMouse::MouseDataFormat());
    const auto* theirs = reinterpret_cast<const std::uint32_t*>(std::uintptr_t(0x00745D00));
    bool ok = std::memcmp(ours, theirs, 5 * sizeof(std::uint32_t)) == 0;
    const auto* a = reinterpret_cast<const std::uint32_t*>(std::uintptr_t(ours[5]));
    const auto* b = reinterpret_cast<const std::uint32_t*>(std::uintptr_t(theirs[5]));
    if (ok) ok = std::memcmp(a, b, theirs[4] * theirs[1]) == 0;
    ei::verify::Check("mouse data format (startup)", true, ok, "%u objects", theirs[4]);
}
} // namespace

EI_STARTUP_CHECK(MouseFormatStartupCheck);
