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

static void __fastcall CursorInitVerify(CCursor* self, void*, CTextureDevice* device, int mode, CDirectInputMouse* mouse, int smallCursors) {
    self->Init(device, mode, mouse, smallCursors);
    const LONG size = static_cast<std::uint8_t>(smallCursors) ? 16 : 32;
    RECT bounds{};
    if (device->primary_) {
        bounds.right = device->renderTarget_->width;
        bounds.bottom = device->renderTarget_->height;
    }
    const bool ok = CursorSize() == size && self->mode_ == mode && self->mouse_ == mouse && self->device_ == device &&
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

// --- The cursor's life cycle and state ------------------------------------------------------------
// All ours only (they take the lock, start or end the drawing thread, read the mouse and the clock).
// Every scenario: the set-up at start (Shutdown first on a reset, then Init, AddShape, Start), the
// shape and held byte from the battle screen, the global map and the camera, the lock from the
// movies and the frame draw, the position update from the drawing thread (vanilla: the thread runs
// unless the cursor is in mode 1).

static void __fastcall ImageDestroyVerify(CursorImage* self) {
    CDXSurface* holder = self->holder;
    self->Destroy();
    ei::verify::Check("CursorImage::Destroy", true, self->holder == holder, "");
}
EI_VERIFY(0x005E5160, ImageDestroyVerify, 0, s_unused);

static CCursor* __fastcall CursorConstructVerify(CCursor* self) {
    CCursor* result = self->Construct();
    const bool ok = result == self && !self->device_ && !self->under_ && !self->target_ && !self->images_.data &&
                    self->images_.size == 0 && self->images_.capacity == 0 && self->images_.growBy == 0 && !self->enabled_;
    ei::verify::Check("CCursor::Construct", true, ok, "");
    return result;
}
EI_VERIFY(0x005E51C0, CursorConstructVerify, 0, s_unused);

static void __fastcall CursorDestroyVerify(CCursor* self) {
    CTextureDevice* device = self->device_;
    CDXSurface* under = self->under_;
    CDXSurface* target = self->target_;
    self->DestroyBody();
    ei::verify::Check("CCursor::DestroyBody", true,
                      self->device_ == device && self->under_ == under && self->target_ == target, "");
}
EI_VERIFY(0x005E4E00, CursorDestroyVerify, 0, s_unused);

static int __fastcall CursorChangeShapeVerify(CCursor* self, void*, int shape) {
    const int previous = self->shape_;
    const bool early = previous == -1 || !self->enabled_;
    const int result = self->ChangeShape(shape);
    bool ok = result == (early ? -1 : previous);
    if (!early) {
        const CursorImage* image = self->image_;
        ok = ok && self->shape_ == shape && image == self->images_.data[shape] && self->from_.top == 0 &&
             self->from_.right - self->from_.left == image->width && self->from_.bottom == image->height;
    }
    ei::verify::Check("CCursor::ChangeShape", true, ok, "shape %d -> %d, result %d", previous, shape, result);
    return result;
}
EI_VERIFY(0x005E5200, CursorChangeShapeVerify, 0, s_unused);

static void __fastcall CursorShowImageVerify(CCursor* self, void*, int shown) {
    const int before = self->imageShown_;
    self->ShowImage(shown);
    ei::verify::Check("CCursor::ShowImage", true, self->imageShown_ == (self->enabled_ ? shown : before), "%d", shown);
}
EI_VERIFY(0x005E5270, CursorShowImageVerify, 0, s_unused);

static void __fastcall CursorSetHeldVerify(CCursor* self, void*, char held) {
    const std::uint8_t before = self->held_;
    self->SetHeld(held);
    const std::uint8_t expected = self->enabled_ ? static_cast<std::uint8_t>(held) : before;
    ei::verify::Check("CCursor::SetHeld", true, self->held_ == expected, "%d", held);
}
EI_VERIFY(0x005E67B0, CursorSetHeldVerify, 0, s_unused);

namespace {
bool OwnsLock(const CCursor* self) {
    return reinterpret_cast<DWORD>(self->lock_.OwningThread) == GetCurrentThreadId() && self->lock_.RecursionCount > 0;
}
} // namespace

static void __fastcall CursorLockVerify(CCursor* self) {
    const bool takes = self->enabled_ && self->mode_ == 2;
    self->TakeLock();
    ei::verify::Check("CCursor::TakeLock", true, !takes || OwnsLock(self), "mode %d", self->mode_);
}
EI_VERIFY(0x005E65F0, CursorLockVerify, 0, s_unused);

static void __fastcall CursorUnlockVerify(CCursor* self) {
    const bool releases = self->enabled_ && self->mode_ == 2;
    const LONG depth = OwnsLock(self) ? self->lock_.RecursionCount : 0;
    self->ReleaseLock();
    const bool ok = !releases || (depth > 1 ? OwnsLock(self) : !OwnsLock(self));
    ei::verify::Check("CCursor::ReleaseLock", true, ok, "mode %d depth %ld", self->mode_, depth);
}
EI_VERIFY(0x005E6610, CursorUnlockVerify, 0, s_unused);

static void __fastcall CursorStartVerify(CCursor* self) {
    void* thread = self->thread_;
    self->Start();
    bool ok = self->shape_ == 0;
    if (self->enabled_)
        ok = ok && self->image_ == self->images_.data[0] && (self->mode_ == 1 ? self->thread_ == thread : self->thread_ != nullptr);
    else
        ok = ok && self->thread_ == thread;
    ei::verify::Check("CCursor::Start", true, ok, "mode %d enabled %d", self->mode_, self->enabled_);
}
EI_VERIFY(0x005E6260, CursorStartVerify, 0, s_unused);

static void __fastcall CursorShutdownVerify(CCursor* self) {
    self->Shutdown();
    const bool ok = !self->thread_ && !self->stopped_ && !self->enabled_ && self->shape_ == -1 && self->mode_ == 0 &&
                    self->images_.size == 0 && !self->images_.data && !self->under_ && !self->target_ && !self->device_ &&
                    !self->image_;
    ei::verify::Check("CCursor::Shutdown", true, ok, "");
}
EI_VERIFY(0x005E62E0, CursorShutdownVerify, 0, s_unused);

static char __fastcall CursorUpdatePositionVerify(CCursor* self) {
    const char moved = self->UpdatePosition();
    const RECT& b = self->bounds_;
    const RECT& r = self->rect_;
    const RECT& f = self->from_;
    bool ok = self->position_.x >= 0 && self->position_.y >= 0 && self->position_.x <= b.right &&
              self->position_.y <= b.bottom && f.right - f.left == r.right - r.left && f.bottom - f.top == r.bottom - r.top &&
              self->image_->frame >= 0 && self->image_->frame < self->image_->frames;
    if (!IsRectEmpty(&r)) ok = ok && r.left >= b.left && r.top >= b.top && r.right <= b.right && r.bottom <= b.bottom;
    ei::verify::Check("CCursor::UpdatePosition", true, ok, "moved %d at (%ld, %ld) rect %ld %ld %ld %ld", moved,
                      self->position_.x, self->position_.y, r.left, r.top, r.right, r.bottom);
    return moved;
}
EI_VERIFY(0x005E52E0, CursorUpdatePositionVerify, 0, s_unused);
