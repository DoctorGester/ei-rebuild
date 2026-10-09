// Checks for the chat overlay's per-frame slots (ui_chat_overlay.cpp). Ours only: they erase
// lines and blit surfaces. Reached only while the overlay is on a window stack (multiplayer chat
// lines); no scenario known to reach it.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/lib/gameapi.h"

#include <cstdint>

namespace {
void* s_unused;

int LineCount(const CChatOverlay* overlay) {
    int n = 0;
    for (auto* node = overlay->lines_.First(); node != overlay->lines_.End(); node = node->next) ++n;
    return n;
}
} // namespace

// 005E2990: afterwards no line is older than 20 s (as of the time read before the call).
static void __fastcall ChatUpdateVerify(CChatOverlay* self) {
    self->Update();
    std::uint32_t now = gameapi::timeGetTime();
    bool ok = true;
    for (auto* node = self->lines_.First(); node != self->lines_.End(); node = node->next)
        if (static_cast<int>(now - node->data.time) > 20000 + 1000) ok = false;
    ei::verify::Check("CChatOverlay::Update", true, ok, "lines %d", LineCount(self));
}
EI_VERIFY(0x005E2990, ChatUpdateVerify, 0, s_unused);

// 005E29E0: the lines are unchanged.
static void __fastcall ChatRedrawVerify(CChatOverlay* self) {
    int before = LineCount(self);
    self->Redraw();
    ei::verify::Check("CChatOverlay::Redraw", before, LineCount(self), "%d");
}
EI_VERIFY(0x005E29E0, ChatRedrawVerify, 0, s_unused);

// 005E26F0: ours only (it allocates): the overlay's vtables, no lines, an empty typed line, closed,
// the caret at 0 and the typed line's surface. Every scenario (CInterface3D::Create).
static CChatOverlay* __fastcall ChatConstructVerify(CChatOverlay* self, void*, CInterface3D* ui) {
    CChatOverlay* result = self->Construct(ui);
    bool ok = result == self && self->vtable_ == reinterpret_cast<void**>(0x007400DC) &&
              self->inputVtable_ == reinterpret_cast<void**>(0x007400A8) && self->interface_ == ui &&
              self->lines_.Empty() && self->typed_.GetLength() == 0 && !self->chatOpen_ && self->caret_ == 0 &&
              self->surfaces_.size == 1;
    ei::verify::Check("CChatOverlay::Construct", true, ok, "surfaces %d lines %d", self->surfaces_.size, LineCount(self));
    return result;
}
EI_VERIFY(0x005E26F0, ChatConstructVerify, 0, s_unused);
