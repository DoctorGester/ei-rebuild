// Check for CInterface3D::CreateScreen (ui_create_screen.cpp).
//
// Ours only (it makes a screen). For the screens whose constructor the original inlines, the
// window at the bottom of the window stack must carry the screen's vtable.
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

std::uintptr_t ExpectedVtable(int id) {
    switch (id) {
    case 1: return 0x00740764;
    case 2: return 0x00740688;
    case 3: return 0x007405AC;
    case 7: return 0x00740840;
    default: return 0;
    }
}
} // namespace

// 005F5BA0: ours only.
static void __fastcall CreateScreenVerify(CInterface3D* self, void*, int id) {
    self->CreateScreen(id);
    std::uintptr_t expected = ExpectedVtable(id);
    if (!expected) return;
    // The activation closes every window and pushes the new one: it is the bottom of the stack.
    void* active = self->stackBegin_ != self->stackEnd_ ? self->stackBegin_[0] : nullptr;
    std::uintptr_t vtable = 0;
    if (active) std::memcpy(&vtable, active, 4);
    ei::verify::Check("CInterface3D::CreateScreen", true, vtable == expected, "id %d: active %p vtable %08X", id, active,
                      static_cast<unsigned>(vtable));
}
EI_VERIFY(0x005F5BA0, CreateScreenVerify, 0, s_unused);
