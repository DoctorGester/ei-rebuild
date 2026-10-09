// Checks for the chat overlay's per-frame slots (ui_chat_overlay.cpp). Ours only: they erase
// lines and blit surfaces. Reached only while the overlay is on a window stack (multiplayer chat
// lines); no scenario known to reach it.
#include "verify.h"

#include "../../src/game/ui.h"
#include "../../src/game/ui_screens.h"
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

// The chat line's opening, clearing and keys (005E36C0, 005E3930, 005E3230, 005E3550): ours only
// (they send chat messages and draw). Multiplayer only (the chat line opens in a network client):
// no scenario. Open: the flag as asked, an opened line empty with the caret at 0; clear: no lines;
// keys: while open afterwards the caret is within the line (the draw clamps it), at most 200
// characters after a typed one.
static void __fastcall ChatSetOpenVerify(CChatOverlay* self, void*, bool open) {
    self->SetOpen(open);
    bool ok = self->chatOpen_ == open && (!open || (self->typed_.GetLength() == 0 && self->caret_ == 0));
    ei::verify::Check("CChatOverlay::SetOpen", true, ok, "open %d caret %d", open, self->caret_);
}
EI_VERIFY(0x005E36C0, ChatSetOpenVerify, 0, s_unused);

static void __fastcall ChatClearLinesVerify(CChatOverlay* self) {
    self->ClearLines();
    ei::verify::Check("CChatOverlay::ClearLines", 0, LineCount(self), "%d");
}
EI_VERIFY(0x005E3930, ChatClearLinesVerify, 0, s_unused);

// 005E2A30: ours only (it makes surfaces and draws): at most 12 lines afterwards (at least the
// ones there before when fewer), each with a surface. Multiplayer only (the client's chat lines):
// no scenario.
static void __fastcall ChatAddLineVerify(CChatOverlay* self, void*, const char* text, std::uint32_t number) {
    int before = LineCount(self);
    self->AddLine(text, number);
    int after = LineCount(self);
    bool ok = after <= 12 && (after >= before || after == 12);
    for (auto* node = self->lines_.First(); node != self->lines_.End(); node = node->next)
        ok = ok && node->data.surface != nullptr;
    ei::verify::Check("CChatOverlay::AddLine", true, ok, "lines %d -> %d", before, after);
}
EI_VERIFY(0x005E2A30, ChatAddLineVerify, 0, s_unused);

namespace {
bool CaretInLine(const CChatOverlay* self) {
    return !self->chatOpen_ || (self->caret_ >= 0 && self->caret_ <= self->typed_.GetLength());
}
} // namespace

static void __fastcall ChatKeyDownVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CChatOverlay>(input);
    self->OnKeyDown(key, repeat, flags);
    ei::verify::Check("CChatOverlay::OnKeyDown", true, CaretInLine(self), "key %d caret %d length %d", key, self->caret_,
                      self->typed_.GetLength());
}
EI_VERIFY(0x005E3230, ChatKeyDownVerify, 0, s_unused);

static void __fastcall ChatCharVerify(void* input, void*, int key, int repeat, int flags) {
    auto* self = WindowFromInput<CChatOverlay>(input);
    self->OnChar(key, repeat, flags);
    bool ok = CaretInLine(self) && self->typed_.GetLength() <= 200;
    ei::verify::Check("CChatOverlay::OnChar", true, ok, "key %d caret %d length %d", key, self->caret_,
                      self->typed_.GetLength());
}
EI_VERIFY(0x005E3550, ChatCharVerify, 0, s_unused);
