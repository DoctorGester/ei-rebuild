// Check for the console window's constructor (005E3CE0, console_window_create.cpp). Ours only (it
// allocates): the result is the window, the screen kept, no surface, no command shown, empty
// input, caret and selection at 0, the history 256 with no more lines than that, not shown, the
// rectangle at least 76 x 69. Every scenario (CInterface3D::Create at startup).
#include "verify.h"

#include "../../src/game/console.h"

namespace {
void* s_unused;
} // namespace

static console::ConsoleWindow* __fastcall ConsoleConstructVerify(console::ConsoleWindow* self, void*, int* screen) {
    console::ConsoleWindow* result = self->Construct(screen);
    bool ok = result == self && self->screen_ == screen && !self->surface_ && self->commandIndex_ == -1 &&
              self->input_.GetLength() == 0 && self->caret_ == 0 && self->anchor_ == 0 && self->scroll_ == 0 &&
              self->history_ == 0x100 && self->lines_.GetSize() <= 0x100 && !self->shown_ && !self->ctrl_ &&
              !self->shift_ && self->width_ >= 76 && self->height_ >= 69;
    ei::verify::Check("ConsoleWindow::Construct", true, ok, "result %p font %p size %dx%d lines %d", result, self->font_,
                      self->width_, self->height_, self->lines_.GetSize());
    return result;
}
EI_VERIFY(0x005E3CE0, ConsoleConstructVerify, 0, s_unused);
