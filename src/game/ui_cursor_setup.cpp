// The software cursor (Cursor, ui_cursor.h): its set-up Init AddShape (called
// by the cursor set-up, cursor_setup.cpp, at start and after a display reset), its life
// cycle, shape and lock and the position update the drawing thread runs. See docs/notes/ui.md, section 3.
#include "ui_cursor.h"

#include "texture.h"

#include "lib/crt.h"
#include "platform/cursor.h"
#include "platform/clock.h"
#include <cstdint>
#include <memory>
#include <string>
#include "platform/thread.h"
#include "game_singletons.h"

#include <cstdlib>

namespace {

constexpr LONG kLargeSize = 0x20;
constexpr LONG kSmallSize = 0x10;

constexpr int kThreadPriority = 2;          // THREAD_PRIORITY_HIGHEST
constexpr int kModeNoThread = 1;
constexpr int kModeLocked = 2;              // the thread draws to the primary: the lock is used
constexpr DWORD kFramePeriod = 125;         // ms per frame of an animated cursor

// `rect` cut to `bounds`; all zero when they do not meet.
void CutToBounds(RECT& rect, const RECT& bounds) {
    if (rect.left < bounds.left) rect.left = bounds.left;
    if (rect.top < bounds.top) rect.top = bounds.top;
    if (rect.right > bounds.right) rect.right = bounds.right;
    if (rect.bottom > bounds.bottom) rect.bottom = bounds.bottom;
    if (rect.left >= rect.right || rect.top >= rect.bottom) rect = RECT{};
}

// A holder pointer emptied, then its old holder released.
void ReleaseHolder(Surface*& slot) {
    Surface* old = slot;
    slot = nullptr;
    if (old) old->Release();
}

// The current image's current frame as the part shown.
void ShowFrame(Cursor* cursor) {
    const CursorImage* image = cursor->image;
    cursor->from.left = image->frame * image->width;
    cursor->from.top = 0;
    cursor->from.right = cursor->from.left + image->width;
    cursor->from.bottom = image->height;
}

} // namespace

void Cursor::Init(TextureDevice* newDevice, int newMode, DirectInputMouse* newMouse, bool smallCursors) {
    const LONG size = smallCursors ? kSmallSize : kLargeSize;
    g_cursorWidth = size;
    g_cursorHeight = size;
    position.x = 0;
    motion.x = 0;
    position.y = 0;
    motion.y = 0;
    image = nullptr;
    imageSpare = 0;
    thread.Close();
    shape = -1;
    imageShown = true;
    enabled = true;
    mode = 0;
    stopped.Create();
    mode = newMode;
    mouse = newMouse;
    TextureDevice* old = device;
    device = newDevice;
    if (newDevice) newDevice->AddRef();
    if (old) old->Release();
    bounds = device->screen;
}

void Cursor::AddShape(const char* name, int hotX, int hotY) {
    const std::string file = std::string(name) + ".mmp";
    images.push_back(new CursorImage(file.c_str(), hotX, hotY));
}

// --- The cursor's life cycle and state ---------------------------------------

CursorImage::~CursorImage() {
    if (holder) holder->Release();
}


Cursor::~Cursor() {
    if (device) device->Release();
}

// The software cursor.
Cursor g_cursor;

int Cursor::SetShape(int newShape) {
    const int previous = shape;
    if (previous == -1 || !enabled) return -1;
    lock.lock();
    shape = newShape;
    image = images[newShape];
    ShowFrame(this);
    lock.unlock();
    return previous;
}

void Cursor::ShowImage(bool shown) {
    if (!enabled) return;
    lock.lock();
    imageShown = shown;
    lock.unlock();
}

void Cursor::SetHeld(bool newHeld) {
    if (!enabled) return;
    lock.lock();
    held = newHeld;
    lock.unlock();
}

void Cursor::TakeLock() {
    if (enabled && mode == kModeLocked) lock.lock();
}

void Cursor::ReleaseLock() {
    if (enabled && mode == kModeLocked) lock.unlock();
}

void Cursor::Start() {
    shape = 0;
    if (!enabled) return;
    lock.lock();
    shape = 0;
    image = images[0];
    ShowFrame(this);
    lock.unlock();
    if (!enabled || mode == kModeNoThread) return;
    stop = 0;
    thread.Start(CursorThreadEntry, this, kThreadPriority);
}

void Cursor::Shutdown() {
    if (thread.Started()) {
        stop = 1;
        stopped.Wait();
        thread.Close();
    }
    stopped.Close();
    for (int i = 0; i < std::ssize(images); ++i) {
        ReleaseHolder(images[i]->holder);
        delete images[i];
    }
    images.clear();
    TextureDevice* currentDevice = device;
    device = nullptr;
    if (currentDevice) currentDevice->Release();
    position.x = 0;
    motion.x = 0;
    position.y = 0;
    motion.y = 0;
    image = nullptr;
    imageSpare = 0;
    thread.Close();
    shape = -1;
    mode = 0;
    enabled = false;
}

bool Cursor::UpdatePosition() {
    // The frame: one step per 125 ms since frameTime (unsigned), wrapped by the frame count.
    const DWORD steps = (platform::Milliseconds() - frameTime) / kFramePeriod;
    image->frame = static_cast<int>(static_cast<unsigned>(image->frame + steps) % static_cast<unsigned>(image->frames));
    frameTime += steps * kFramePeriod;
    bool moved = steps != 0;

    const std::int32_t* state = mouse->Poll();
    motion.x += state[0];
    motion.y += state[1];
    POINT point;
    if (held) {
        point = heldPosition;
    } else {
        platform::Point at{};
        platform::CursorPosition(&at);
        point = POINT{at.x, at.y};
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
    CutToBounds(rect, bounds);
    from.left = from.left + rect.left - left;
    from.top = from.top + rect.top - top;
    from.right = rect.right - rect.left + from.left;
    from.bottom = rect.bottom - rect.top + from.top;
    position = point;
    return moved;
}
