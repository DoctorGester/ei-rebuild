// The software cursor (CCursor, ui_cursor.h): its set-up Init 005E60B0 and AddShape 005E63F0 (called
// by the cursor set-up 005DAEA0, cursor_setup.cpp, at start and after a display reset), its life
// cycle, shape and lock (005E4E00-005E67B0) and the position update the drawing thread runs
// (005E52E0). docs/notes/ui.md, section 3.
#include "ui_cursor.h"

#include "texture.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "originals_render.h"

namespace {

// The cursor's size in pixels (007C7C90 / 007C7C94): 32, or 16 for the small cursors.
LONG& CursorWidth() { return *reinterpret_cast<LONG*>(0x007C7C90); }
LONG& CursorHeight() { return *reinterpret_cast<LONG*>(0x007C7C94); }
constexpr LONG kLargeSize = 0x20;
constexpr LONG kSmallSize = 0x10;

// A surface holder pointer set to `holder` (one reference more), the old one released.
void AssignHolder(CDXSurface*& slot, CDXSurface* holder) {
    CDXSurface* old = slot;
    slot = holder;
    if (holder) ++holder->refCount;
    if (old) old->Release();
}

// A new off-screen surface of the screen's format (004EF7B0 through its address); null when the
// allocation fails.
CDXSurface* NewSurface(CTextureDevice* device, LONG width, LONG height) {
    void* memory = gamecrt::operator_new(sizeof(CDXSurface));
    if (!memory) return nullptr;
    return reinterpret_cast<CDXSurface*>(DXSurfaceConstructOriginal(memory, nullptr, device, width, height));
}

} // namespace

void CCursor::Init(CTextureDevice* device, int mode, CDirectInputMouse* mouse, int smallCursors) {
    const LONG size = static_cast<std::uint8_t>(smallCursors) ? kSmallSize : kLargeSize;
    CursorWidth() = size;
    CursorHeight() = size;
    position_.x = 0;
    motion_.x = 0;
    position_.y = 0;
    motion_.y = 0;
    image_ = nullptr;
    unknown_14 = 0;
    thread_ = nullptr;
    shape_ = -1;
    imageShown_ = 1;
    enabled_ = 1;
    mode_ = 0;
    stopped_ = gameapi::CreateEventA(nullptr, FALSE, FALSE, nullptr);
    mode_ = mode;
    mouse_ = mouse;
    CTextureDevice* old = device_;
    device_ = device;
    if (device) device->AddRef();
    if (old) old->Release();
    gameapi::SetRectEmpty(&bounds_);
    if (device_->primary_) {
        bounds_.right = device_->renderTarget_->width;
        bounds_.bottom = device_->renderTarget_->height;
    }
    AssignHolder(under_, NewSurface(device_, CursorWidth(), CursorHeight()));
    AssignHolder(target_, NewSurface(device_, CursorWidth() * 2, CursorHeight() * 2));
}
EI_REPLACE_METHOD(0x005E60B0, CCursor::Init);

void CCursor::AddShape(const char* name, int hotX, int hotY, int flag) {
    CursorImage* image = nullptr;
    if (void* memory = gamecrt::operator_new(sizeof(CursorImage))) {
        mfc::CString file = mfc::CString(name) + ".mmp";
        image = CursorImageConstructOriginal(memory, nullptr, static_cast<const char*>(file), hotX, hotY, flag);
    }
    images_.Add(image);
}
EI_REPLACE_METHOD(0x005E63F0, CCursor::AddShape);

// --- The cursor's life cycle and state (005E4E00-005E67B0) ---------------------------------------

namespace {

// 005E52A0: the drawing thread's procedure (cdecl: the cursor; runs 005E5690).
void* CursorThreadProcedure() { return reinterpret_cast<void*>(0x005E52A0); }
constexpr int kThreadPriority = 2;          // THREAD_PRIORITY_HIGHEST
constexpr int kModeNoThread = 1;
constexpr int kModeLocked = 2;              // the thread draws to the primary: the lock is used
constexpr DWORD kFramePeriod = 125;         // ms per frame of an animated cursor

// A holder pointer emptied, then its old holder released.
void ReleaseHolder(CDXSurface*& slot) {
    CDXSurface* old = slot;
    slot = nullptr;
    if (old) old->Release();
}

} // namespace

void CursorImage::Destroy() {
    gamecrt::operator_delete(saved);
    if (holder) holder->Release();
}
EI_REPLACE_METHOD(0x005E5160, CursorImage::Destroy);

CCursor* CCursor::Construct() {
    device_ = nullptr;
    under_ = nullptr;
    target_ = nullptr;
    images_.data = nullptr;
    images_.growBy = 0;
    images_.capacity = 0;
    images_.size = 0;
    gameapi::InitializeCriticalSection(&lock_);
    enabled_ = 0;
    return this;
}
EI_REPLACE_METHOD(0x005E51C0, CCursor::Construct);

void CCursor::DestroyBody() {
    gameapi::DeleteCriticalSection(&lock_);
    images_.FreeData();
    if (target_) target_->Release();
    if (under_) under_->Release();
    if (device_) device_->Release();
}
EI_REPLACE_METHOD(0x005E4E00, CCursor::DestroyBody);

namespace {
// The current image's current frame as the part shown.
void ShowFrame(CCursor* cursor) {
    const CursorImage* image = cursor->image_;
    cursor->from_.left = image->frame * image->width;
    cursor->from_.top = 0;
    cursor->from_.right = cursor->from_.left + image->width;
    cursor->from_.bottom = image->height;
}
} // namespace

int CCursor::ChangeShape(int shape) {
    const int previous = shape_;
    if (previous == -1 || !enabled_) return -1;
    gameapi::EnterCriticalSection(&lock_);
    shape_ = shape;
    image_ = images_.data[shape];
    ShowFrame(this);
    gameapi::LeaveCriticalSection(&lock_);
    return previous;
}
EI_REPLACE_METHOD(0x005E5200, CCursor::ChangeShape);

void CCursor::ShowImage(int shown) {
    if (!enabled_) return;
    gameapi::EnterCriticalSection(&lock_);
    imageShown_ = shown;
    gameapi::LeaveCriticalSection(&lock_);
}
EI_REPLACE_METHOD(0x005E5270, CCursor::ShowImage);

void CCursor::SetHeld(char held) {
    if (!enabled_) return;
    gameapi::EnterCriticalSection(&lock_);
    held_ = static_cast<std::uint8_t>(held);
    gameapi::LeaveCriticalSection(&lock_);
}
EI_REPLACE_METHOD(0x005E67B0, CCursor::SetHeld);

void CCursor::TakeLock() {
    if (enabled_ && mode_ == kModeLocked) gameapi::EnterCriticalSection(&lock_);
}
EI_REPLACE_METHOD(0x005E65F0, CCursor::TakeLock);

void CCursor::ReleaseLock() {
    if (enabled_ && mode_ == kModeLocked) gameapi::LeaveCriticalSection(&lock_);
}
EI_REPLACE_METHOD(0x005E6610, CCursor::ReleaseLock);

void CCursor::Start() {
    shape_ = 0;
    if (!enabled_) return;
    gameapi::EnterCriticalSection(&lock_);
    shape_ = 0;
    image_ = images_.data[0];
    ShowFrame(this);
    gameapi::LeaveCriticalSection(&lock_);
    if (!enabled_ || mode_ == kModeNoThread) return;
    stop_ = 0;
    thread_ = AfxBeginThreadOriginal(CursorThreadProcedure(), this, kThreadPriority, 0, 0, nullptr);
}
EI_REPLACE_METHOD(0x005E6260, CCursor::Start);

void CCursor::Shutdown() {
    if (thread_) {
        stop_ = 1;
        gameapi::WaitForSingleObject(stopped_, INFINITE);
    }
    gameapi::CloseHandle(stopped_);
    stopped_ = nullptr;
    for (int i = 0; i < images_.size; ++i) {
        ReleaseHolder(images_.data[i]->holder);
        if (CursorImage* image = images_.data[i]) {
            image->Destroy();
            gamecrt::operator_delete(image);
        }
    }
    images_.SetSize(0, -1);
    ReleaseHolder(under_);
    ReleaseHolder(target_);
    CTextureDevice* device = device_;
    device_ = nullptr;
    if (device) device->Release();
    position_.x = 0;
    motion_.x = 0;
    position_.y = 0;
    motion_.y = 0;
    image_ = nullptr;
    unknown_14 = 0;
    thread_ = nullptr;
    shape_ = -1;
    mode_ = 0;
    enabled_ = 0;
}
EI_REPLACE_METHOD(0x005E62E0, CCursor::Shutdown);

char CCursor::UpdatePosition() {
    // The frame: one step per 125 ms since frameTime_ (unsigned), wrapped by the frame count.
    const DWORD steps = (gameapi::timeGetTime() - frameTime_) / kFramePeriod;
    image_->frame = static_cast<int>(static_cast<unsigned>(image_->frame + steps) % static_cast<unsigned>(image_->frames));
    frameTime_ += steps * kFramePeriod;
    char moved = steps != 0;

    const LONG* state = mouse_->PollThroughHook(2);
    motion_.x += state[0];
    motion_.y += state[1];
    POINT point;
    if (held_) {
        point = heldPosition_;
    } else {
        gameapi::GetCursorPos(&point);
        heldPosition_ = point;
    }
    if (point.x < 0) point.x = 0;
    if (point.y < 0) point.y = 0;
    if (point.x > bounds_.right) point.x = bounds_.right;
    if (point.y > bounds_.bottom) point.y = bounds_.bottom;
    if (position_.x != point.x || position_.y != point.y) moved = 1;

    ShowFrame(this);
    const LONG left = position_.x - image_->hotX;
    const LONG top = position_.y - image_->hotY;
    rect_.left = left;
    rect_.top = top;
    rect_.right = from_.right - from_.left + left;
    rect_.bottom = from_.bottom - from_.top + top;
    gameapi::IntersectRect(&rect_, &rect_, &bounds_);
    from_.left = from_.left + rect_.left - left;
    from_.top = from_.top + rect_.top - top;
    from_.right = rect_.right - rect_.left + from_.left;
    from_.bottom = rect_.bottom - rect_.top + from_.top;
    position_ = point;
    return moved;
}
EI_REPLACE_METHOD(0x005E52E0, CCursor::UpdatePosition);
