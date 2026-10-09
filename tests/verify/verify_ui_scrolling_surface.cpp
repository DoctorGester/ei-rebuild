// Checks for ui_scrolling_surface.cpp (A-H17). Scenarios: ui_main_menu and render_titles_present
// (the titles: Load renders the text once, then every frame of a scrolling line).
// 0065F020 draws into the surface through GDI and a RichEdit, so ours runs alone: the end offset
// only ever becomes the current offset or 0 (with the speed 0 when it is cleared), and the
// offsets themselves do not move.
#include "verify.h"

#include "../../src/game/ui_screens.h"

namespace {
void* s_unused;

void __fastcall RenderTextVerify(CUiScrollingSurface* self) {
    int endBefore = self->endOffset_;
    int offset = self->offset_;
    int offsetX = self->offsetX_;
    self->RenderText();
    bool ended = self->endOffset_ == 0 && endBefore > 0;
    bool ok = self->offset_ == offset && self->offsetX_ == offsetX &&
              (self->endOffset_ == endBefore || (endBefore <= 0 && self->endOffset_ == offset) ||
               (ended && !(self->speed_ < 0.0f || self->speed_ > 0.0f)));
    ei::verify::Check("CUiScrollingSurface::RenderText", true, ok, "surface %p: offset %d end %d -> %d", self, offset,
                      endBefore, self->endOffset_);
}
} // namespace

EI_VERIFY(0x0065F020, RenderTextVerify, 0, s_unused);
