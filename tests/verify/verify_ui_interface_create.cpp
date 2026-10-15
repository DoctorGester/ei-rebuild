// Check for the vanilla body of the interface's Create 005F4990 (A-I22,
// src/game/ui_interface_create.cpp). Rebirth patches it (WideScreen's font scale), which the
// rewrite reproduces, so it runs in both launches; with WideScreen the scale is checked against
// the float addon wrote at 005F4F81 (verify_ui_widescreen.cpp).
//
// Ours only: it makes the fonts, the console window, the chat overlay and the fade, changes the
// keyboard layout and runs once. Read back: the screen size is the render target's; the three
// fonts have handles and grow in height (shares 104 < 112 < 144 of the width); the console, the
// chat overlay and the fade exist; the state it clears is clear; every help topic read has a name.
// The start of every scenario (the application's start calls it once).
#include "verify.h"

#include "../../src/game/console.h"
#include "../../src/game/texture.h"
#include "../../src/game/ui.h"
#include "../../src/game/ui_gdi.h"
#include "../../src/lib/gameapi.h"

namespace {

void* s_unused;

int FontHeight(const ui_gdi::GameFont* font) {
    if (!font || !font->handle_) return 0;
    LOGFONTA info{};
    gameapi::GetObjectA(font->handle_, sizeof(info), &info);
    return info.lfHeight < 0 ? -info.lfHeight : info.lfHeight;
}

} // namespace

static void __fastcall InterfaceCreateVerify(CInterface3D* self, void*, CTextureDevice* device) {
    self->Create(device);
    bool size = !device->primary_ ||
                (self->width_ == device->renderTarget_->width && self->height_ == device->renderTarget_->height);
    const int heights[3] = {FontHeight(self->fonts_[0]), FontHeight(self->fonts_[1]), FontHeight(self->fonts_[2])};
    bool fonts = heights[0] > 0 && heights[0] <= heights[1] && heights[1] <= heights[2];
    bool made = self->console_ != nullptr && self->root_ != nullptr && self->fade_ != nullptr;
    bool clear = self->mouseCapture_ == nullptr && self->backdrop_ == nullptr && self->secondBackdrop_ == nullptr &&
                 self->block5C_ == nullptr && self->backdropMode_ == 0 && !self->closeAll_ && self->queuedWindow_ == nullptr &&
                 self->drawing_ && self->fpsSurface_ == nullptr && self->fpsFrames_ == 0 && self->messageFlag_ == 0 &&
                 !self->ctrlDown_ && !self->altDown_ && !self->rightDown_;
    using Topics = stlp::map<mfc::CString, int>;
    int topics = 0;
    bool named = true;
    for (Topics::Node* n = self->helpTopics_.begin(); n != self->helpTopics_.end(); n = Topics::Next(n)) {
        ++topics;
        named = named && static_cast<const char*>(n->first)[0] != 0;
    }
    named = named && topics == static_cast<int>(self->helpTopics_.size());
    ei::verify::Check("CInterface3D::Create", true, size && fonts && made && clear && named,
                      "size %d (%d x %d) fonts %d (%d %d %d) made %d clear %d topics %d (%d)", size, self->width_,
                      self->height_, fonts, heights[0], heights[1], heights[2], made, clear, named, topics);
}
EI_VERIFY(0x005F4990, InterfaceCreateVerify, 0, s_unused);
