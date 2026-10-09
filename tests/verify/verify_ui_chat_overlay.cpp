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
