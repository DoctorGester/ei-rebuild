// Checks for the cursor's set-up (ui_cursor_setup.cpp): 005E60B0 CCursor::Init and 005E63F0
// CCursor::AddShape. Ours only (they create an event, surfaces and images): Init leaves the
// arguments in their fields, the size globals at 16 or 32, the shape unset, the cursor enabled,
// the bounds at the render target's size (or empty) and two surface holders of the cursor size and
// twice that; AddShape appends one entry (the image, or null) and keeps the others. Every
// scenario: the set-up 005DAEA0 runs at start (25 shapes).
#include "verify.h"

#include "../../src/game/texture.h"
#include "../../src/game/ui_cursor.h"

#include <vector>

namespace {
void* s_unused;

LONG CursorSize() { return *reinterpret_cast<const LONG*>(0x007C7C90); }

bool HolderOk(const CDXSurface* holder, LONG size) {
    return holder && holder->refCount >= 1 && holder->width == size && holder->height == size;
}
} // namespace

static void __fastcall CursorInitVerify(CCursor* self, void*, CTextureDevice* device, int mode, int context, int smallCursors) {
    self->Init(device, mode, context, smallCursors);
    const LONG size = static_cast<std::uint8_t>(smallCursors) ? 16 : 32;
    RECT bounds{};
    if (device->primary_) {
        bounds.right = device->renderTarget_->width;
        bounds.bottom = device->renderTarget_->height;
    }
    const bool ok = CursorSize() == size && self->mode_ == mode && self->context_ == context && self->device_ == device &&
                    self->shape_ == -1 && self->imageShown_ == 1 && self->enabled_ == 1 && !self->image_ &&
                    self->stopped_ && EqualRect(&self->bounds_, &bounds) && HolderOk(self->under_, size) &&
                    HolderOk(self->target_, size * 2);
    ei::verify::Check("CCursor::Init", true, ok, "mode %d small %d size %ld", mode, smallCursors, CursorSize());
}
EI_VERIFY(0x005E60B0, CursorInitVerify, 0, s_unused);

static void __fastcall CursorAddShapeVerify(CCursor* self, void*, const char* name, int hotX, int hotY, int flag) {
    std::vector<CursorImage*> before(self->images_.data, self->images_.data + self->images_.size);
    self->AddShape(name, hotX, hotY, flag);
    bool ok = self->images_.size == static_cast<int>(before.size()) + 1 && self->images_.capacity >= self->images_.size;
    for (std::size_t i = 0; ok && i < before.size(); ++i) ok = self->images_.data[i] == before[i];
    const CursorImage* image = ok ? self->images_.data[before.size()] : nullptr;
    ok = ok && (!image || (image->holder && image->frames >= 1));
    ei::verify::Check("CCursor::AddShape", true, ok, "%s: %zu -> %d", name, before.size(), self->images_.size);
}
EI_VERIFY(0x005E63F0, CursorAddShapeVerify, 0, s_unused);
