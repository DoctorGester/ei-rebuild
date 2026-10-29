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
    const CursorImage* image = cursor->image;
    cursor->from.left = image->frame * image->width;
    cursor->from.top = 0;
    cursor->from.right = cursor->from.left + image->width;
    cursor->from.bottom = image->height;
}

} // namespace

void CCursor::Init(CTextureDevice* newDevice, int newMode, CDirectInputMouse* newMouse, bool smallCursors) {
    const LONG size = smallCursors ? kSmallSize : kLargeSize;
    g_cursorWidth = size;
    g_cursorHeight = size;
    position.x = 0;
    motion.x = 0;
    position.y = 0;
    motion.y = 0;
    image = nullptr;
    imageSpare = 0;
    thread = nullptr;
    shape = -1;
    imageShown = true;
    enabled = true;
    mode = 0;
    stopped = gameapi::CreateEventA(nullptr, FALSE, FALSE, nullptr);
    mode = newMode;
    mouse = newMouse;
    CTextureDevice* old = device;
    device = newDevice;
    if (newDevice) newDevice->AddRef();
    if (old) old->Release();
    bounds = device->screen;
}

void CCursor::AddShape(const char* name, int hotX, int hotY) {
    const std::string file = std::string(name) + ".mmp";
    images.push_back(new CursorImage(file.c_str(), hotX, hotY));
}

// --- The cursor's life cycle and state ---------------------------------------

CursorImage::~CursorImage() {
    if (holder) holder->Release();
}

CCursor::CCursor() { gameapi::InitializeCriticalSection(&lock); }

CCursor::~CCursor() {
    gameapi::DeleteCriticalSection(&lock);
    if (device) device->Release();
}

// The software cursor.
CCursor g_cursor;

int CCursor::SetShape(int newShape) {
    const int previous = shape;
    if (previous == -1 || !enabled) return -1;
    gameapi::EnterCriticalSection(&lock);
    shape = newShape;
    image = images[newShape];
    ShowFrame(this);
    gameapi::LeaveCriticalSection(&lock);
    return previous;
}

void CCursor::ShowImage(bool shown) {
    if (!enabled) return;
    gameapi::EnterCriticalSection(&lock);
    imageShown = shown;
    gameapi::LeaveCriticalSection(&lock);
}

void CCursor::SetHeld(bool newHeld) {
    if (!enabled) return;
    gameapi::EnterCriticalSection(&lock);
    held = newHeld;
    gameapi::LeaveCriticalSection(&lock);
}

void CCursor::TakeLock() {
    if (enabled && mode == kModeLocked) gameapi::EnterCriticalSection(&lock);
}

void CCursor::ReleaseLock() {
    if (enabled && mode == kModeLocked) gameapi::LeaveCriticalSection(&lock);
}

void CCursor::Start() {
    shape = 0;
    if (!enabled) return;
    gameapi::EnterCriticalSection(&lock);
    shape = 0;
    image = images[0];
    ShowFrame(this);
    gameapi::LeaveCriticalSection(&lock);
    if (!enabled || mode == kModeNoThread) return;
    stop = 0;
    thread = lib::StartThread(CursorThreadEntry, this, kThreadPriority);
}

void CCursor::Shutdown() {
    if (thread) {
        stop = 1;
        gameapi::WaitForSingleObject(stopped, INFINITE);
        gameapi::CloseHandle(thread);
    }
    gameapi::CloseHandle(stopped);
    stopped = nullptr;
    for (int i = 0; i < std::ssize(images); ++i) {
        ReleaseHolder(images[i]->holder);
        delete images[i];
    }
    images.clear();
    CTextureDevice* currentDevice = device;
    device = nullptr;
    if (currentDevice) currentDevice->Release();
    position.x = 0;
    motion.x = 0;
    position.y = 0;
    motion.y = 0;
    image = nullptr;
    imageSpare = 0;
    thread = nullptr;
    shape = -1;
    mode = 0;
    enabled = false;
}

bool CCursor::UpdatePosition() {
    // The frame: one step per 125 ms since frameTime (unsigned), wrapped by the frame count.
    const DWORD steps = (gameapi::timeGetTime() - frameTime) / kFramePeriod;
    image->frame = static_cast<int>(static_cast<unsigned>(image->frame + steps) % static_cast<unsigned>(image->frames));
    frameTime += steps * kFramePeriod;
    bool moved = steps != 0;

    const LONG* state = mouse->Poll();
    motion.x += state[0];
    motion.y += state[1];
    POINT point;
    if (held) {
        point = heldPosition;
    } else {
        gameapi::GetCursorPos(&point);
        heldPosition = point;
    }
    if (point.x < 0) point.x = 0;
    if (point.y < 0) point.y = 0;
    if (point.x > bounds.right) point.x = bounds.right;
    if (point.y > bounds.bottom) point.y = bounds.bottom;
    if (position.x != point.x || position.y != point.y) moved = true;

    ShowFrame(this);
    const LONG left = position.x - image->hotX;
    const LONG top = position.y - image->hotY;
    rect.left = left;
    rect.top = top;
    rect.right = from.right - from.left + left;
    rect.bottom = from.bottom - from.top + top;
    gameapi::IntersectRect(&rect, &rect, &bounds);
    from.left = from.left + rect.left - left;
    from.top = from.top + rect.top - top;
    from.right = rect.right - rect.left + from.left;
    from.bottom = rect.bottom - rect.top + from.top;
    position = point;
    return moved;
}
