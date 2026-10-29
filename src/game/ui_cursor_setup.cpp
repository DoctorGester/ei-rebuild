// The software cursor (CCursor, ui_cursor.h): its set-up Init AddShape (called
// by the cursor set-up, cursor_setup.cpp, at start and after a display reset), its life
// cycle, shape and lock and the position update the drawing thread runs. See docs/notes/ui.md, section 3.
#include "ui_cursor.h"

#include "texture.h"

#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <memory>
#include <string>
#include "lib/thread.h"
#include "game_singletons.h"

#include <cstdlib>

namespace {

constexpr LONG kLargeSize = 0x20;
constexpr LONG kSmallSize = 0x10;

constexpr int kThreadPriority = 2;          // THREAD_PRIORITY_HIGHEST
constexpr int kModeNoThread = 1;
constexpr int kModeLocked = 2;              // the thread draws to the primary: the lock is used
constexpr DWORD kFramePeriod = 125;         // ms per frame of an animated cursor

// A holder pointer emptied, then its old holder released.
void ReleaseHolder(Surface*& slot) {
    Surface* old = slot;
    slot = nullptr;
    if (old) old->Release();
}

// The current image's current frame as the part shown.
void ShowFrame(CCursor* cursor) {
    const CursorImage* image = cursor->image_;
    cursor->from_.left = image->frame * image->width;
    cursor->from_.top = 0;
    cursor->from_.right = cursor->from_.left + image->width;
    cursor->from_.bottom = image->height;
}

} // namespace

void CCursor::Init(CTextureDevice* device, int mode, CDirectInputMouse* mouse, bool smallCursors) {
    const LONG size = smallCursors ? kSmallSize : kLargeSize;
    g_cursorWidth = size;
    g_cursorHeight = size;
    position_.x = 0;
    motion_.x = 0;
    position_.y = 0;
    motion_.y = 0;
    image_ = nullptr;
    imageSpare_ = 0;
    thread_ = nullptr;
    shape_ = -1;
    imageShown_ = true;
    enabled_ = true;
    mode_ = 0;
    stopped_ = gameapi::CreateEventA(nullptr, FALSE, FALSE, nullptr);
    mode_ = mode;
    mouse_ = mouse;
    CTextureDevice* old = device_;
    device_ = device;
    if (device) device->AddRef();
    if (old) old->Release();
    bounds_ = device_->screen_;
}

void CCursor::AddShape(const char* name, int hotX, int hotY) {
    const std::string file = std::string(name) + ".mmp";
    images_.push_back(new CursorImage(file.c_str(), hotX, hotY));
}

// --- The cursor's life cycle and state ---------------------------------------

CursorImage::~CursorImage() {
    if (holder) holder->Release();
}

CCursor::CCursor() { gameapi::InitializeCriticalSection(&lock_); }

CCursor::~CCursor() {
    gameapi::DeleteCriticalSection(&lock_);
    if (device_) device_->Release();
}

// The software cursor.
CCursor g_cursor;

int CCursor::SetShape(int shape) {
    const int previous = shape_;
    if (previous == -1 || !enabled_) return -1;
    gameapi::EnterCriticalSection(&lock_);
    shape_ = shape;
    image_ = images_[shape];
    ShowFrame(this);
    gameapi::LeaveCriticalSection(&lock_);
    return previous;
}

void CCursor::ShowImage(bool shown) {
    if (!enabled_) return;
    gameapi::EnterCriticalSection(&lock_);
    imageShown_ = shown;
    gameapi::LeaveCriticalSection(&lock_);
}

void CCursor::SetHeld(bool held) {
    if (!enabled_) return;
    gameapi::EnterCriticalSection(&lock_);
    held_ = held;
    gameapi::LeaveCriticalSection(&lock_);
}

void CCursor::TakeLock() {
    if (enabled_ && mode_ == kModeLocked) gameapi::EnterCriticalSection(&lock_);
}

void CCursor::ReleaseLock() {
    if (enabled_ && mode_ == kModeLocked) gameapi::LeaveCriticalSection(&lock_);
}

void CCursor::Start() {
    shape_ = 0;
    if (!enabled_) return;
    gameapi::EnterCriticalSection(&lock_);
    shape_ = 0;
    image_ = images_[0];
    ShowFrame(this);
    gameapi::LeaveCriticalSection(&lock_);
    if (!enabled_ || mode_ == kModeNoThread) return;
    stop_ = 0;
    thread_ = lib::StartThread(CursorThreadEntry, this, kThreadPriority);
}

void CCursor::Shutdown() {
    if (thread_) {
        stop_ = 1;
        gameapi::WaitForSingleObject(stopped_, INFINITE);
        gameapi::CloseHandle(thread_);
    }
    gameapi::CloseHandle(stopped_);
    stopped_ = nullptr;
    for (int i = 0; i < std::ssize(images_); ++i) {
        ReleaseHolder(images_[i]->holder);
        delete images_[i];
    }
    images_.clear();
    CTextureDevice* device = device_;
    device_ = nullptr;
    if (device) device->Release();
    position_.x = 0;
    motion_.x = 0;
    position_.y = 0;
    motion_.y = 0;
    image_ = nullptr;
    imageSpare_ = 0;
    thread_ = nullptr;
    shape_ = -1;
    mode_ = 0;
    enabled_ = false;
}

bool CCursor::UpdatePosition() {
    // The frame: one step per 125 ms since frameTime_ (unsigned), wrapped by the frame count.
    const DWORD steps = (gameapi::timeGetTime() - frameTime_) / kFramePeriod;
    image_->frame = static_cast<int>(static_cast<unsigned>(image_->frame + steps) % static_cast<unsigned>(image_->frames));
    frameTime_ += steps * kFramePeriod;
    bool moved = steps != 0;

    const LONG* state = mouse_->Poll();
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
    if (position_.x != point.x || position_.y != point.y) moved = true;

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
