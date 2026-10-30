// The cursor images' constructor (the cursor object, see docs/notes/ui.md, "Cursor"), which builds each image's mask
// from its picture's pixels in the .mmp file (the holder has no memory). (The software cursor's blit, which combined
// image, mask and background through Locks of the holders' surfaces, has no caller of ours since: the new renderer
// draws the cursor, ui_cursor.cpp.)
#include "ui_cursor.h"

#include "game_singletons.h"
#include "model.h"
#include "surface.h"
#include "texture.h"
#include "world.h"
#include "lib/crt.h"
#include "platform/clock.h"
#include "platform/cursor.h"
#include "platform/thread.h"
#include "render/render.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

// --- a cursor image ------------------------------------------------------------------------------

namespace {

// The mask of the 8888 picture at `bitsPerPixel` (16 or 32) a pixel, its rows one after another: all ones where the
// pixel's colour in the screen's format is 0 (16 bits 5:6:5: the whole pixel; 32 bits: its low 24 bits), else 0.
void BuildMask(const MmpHeader* picture, int bitsPerPixel, std::uint8_t* mask) {
    ChannelLayout layout;
    layout.Split(ScreenFormat(bitsPerPixel));
    const std::size_t bytes = static_cast<std::size_t>(bitsPerPixel / 8);
    const std::uint8_t* pixels = reinterpret_cast<const std::uint8_t*>(picture) + sizeof(MmpHeader) + picture->MipOffset(0);
    const std::size_t count = static_cast<std::size_t>(picture->width) * picture->height;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t argb;
        std::memcpy(&argb, pixels + i * 4, sizeof(argb));
        std::memset(mask + i * bytes, layout.ToNative(argb) == 0 ? 0xFF : 0, bytes);
    }
}

} // namespace

CursorImage::CursorImage(const char* name, int hotSpotX, int hotSpotY) {
    holder = nullptr;
    frame = 0;
    TextureManager* manager = g_clientWorld.textureManager;
    MmpHeader* picture = manager->GetFileData(name);
    // (A throw passes the exception on; that ends the game.)
    holder = new Surface(g_sceneRenderer.device, picture);
    holder->AddRef();

    // The holder took the picture's size and the screen's pixel size or threw.
    std::uint32_t pictureWidth = static_cast<std::uint32_t>(holder->width), pictureHeight = static_cast<std::uint32_t>(holder->height);
    std::uint32_t count = pictureWidth / pictureHeight;
    height = static_cast<std::int32_t>(pictureHeight);
    frames = static_cast<int>(count);
    width = static_cast<std::int32_t>(pictureWidth / count);
    hotX = static_cast<int>(g_cursorWidth) * hotSpotX / 32;
    hotY = static_cast<int>(g_cursorHeight) * hotSpotY / 32;
    std::uint32_t maskBytes = (static_cast<std::uint32_t>(holder->bitsPerPixel) * pictureHeight * pictureWidth) >> 3;
    saved.assign(maskBytes, 0);
    BuildMask(picture, holder->bitsPerPixel, saved.data());
    manager->CloseFile(picture);
}

// --- Frame -------------------------------------------------------------------------------------------

// The software mouse cursor in the frame: the image and its mask for the cursor's rectangle go to the new renderer
// (render::Cursor), which draws it in every frame it presents; the cursor thread moves it between frames by presenting
// the last frame again (render::CursorBetweenFrames), not while the image is hidden. The DirectDraw background saves
// and composition are left out. See docs/notes/ui.md, "Cursor", and docs/notes/renderer.md.

namespace {

constexpr std::uint32_t kThreadPeriod = 41;                  // ms between the thread's redraws

// The renderer object; its flags' bit 0 is the software cursor display option.
bool SoftwareCursor() { return (g_sceneRenderer.flags & 1) != 0; }

render::Rect ToRender(const Rect& r) { return render::Rect{r.left, r.top, r.right, r.bottom}; }

// The cursor thread's step: under the cursor's lock, the position updated and,
// when an image is shown and the cursor moved (or its frame stepped), the last frame presented again
// with the cursor at its new rectangle (render::CursorBetweenFrames; nothing while the main thread's
// frames come often enough); the drawn rectangle follows as the DirectDraw blits made it.
void MoveBetweenFrames(Cursor* self) {
    self->lock.lock();
    const bool moved = self->UpdatePosition();
    if (self->imageShown && moved) {
        CursorImage* image = self->image;
        render::CursorBetweenFrames(image->holder, image->saved.data(), ToRender(self->from), ToRender(self->rect));
        self->drawn = self->rect;
    }
    self->lock.unlock();
}

} // namespace

// (Without an image shown, DirectDraw saves the background from the primary in mode 2: nothing here.)
void CursorDrawFrame(Cursor* self) {
    if (!SoftwareCursor()) return;
    if (!self->enabled) return;
    self->UpdatePosition();
    Rect* rect = &self->rect;
    if (rect->left >= rect->right || rect->top >= rect->bottom) return;
    if (!self->imageShown) return;
    CursorImage* image = self->image;
    render::Cursor(image->holder, image->saved.data(), ToRender(self->from), ToRender(*rect));
    self->drawn = *rect;
}

// Takes the cursor's place once, then until `stop` is set: every 41 ms, with the software cursor on, one
// MoveBetweenFrames (DirectDraw: when an image is shown and the cursor moved, its old place's background
// restored, the new one's saved and the composed cursor blitted to the primary, after the vertical blank). At
// the end the event `stopped` is set.
void CursorThread(Cursor* self) {
    if (!self->enabled) return;
    self->lock.lock();
    self->UpdatePosition();
    self->drawn = self->rect;
    self->lock.unlock();
    std::uint32_t last = platform::Milliseconds();
    while (!self->stop) {
        std::uint32_t now = platform::Milliseconds();
        if (now - last < kThreadPeriod) {
            platform::Sleep(kThreadPeriod - (now - last));
            continue;
        }
        last = now;
        if (!SoftwareCursor()) continue;
        MoveBetweenFrames(self);
    }
    self->stopped.Set();
}

// The procedure platform::Thread starts (the cursor as its parameter): CursorThread, then exit code 0.
unsigned __stdcall CursorThreadEntry(void* cursor) {
    CursorThread(static_cast<Cursor*>(cursor));
    return 0;
}

// --- Setup -------------------------------------------------------------------------------------------

// The software cursor (Cursor, ui_cursor.h): its set-up Init AddShape (called
// by the cursor set-up, cursor_setup.cpp, at start and after a display reset), its life
// cycle, shape and lock and the position update the drawing thread runs. See docs/notes/ui.md, section 3.

namespace {

constexpr std::int32_t kLargeSize = 0x20;
constexpr std::int32_t kSmallSize = 0x10;

constexpr int kThreadPriority = 2;          // THREAD_PRIORITY_HIGHEST
constexpr int kModeNoThread = 1;
constexpr int kModeLocked = 2;              // the thread draws to the primary: the lock is used
constexpr std::uint32_t kFramePeriod = 125;         // ms per frame of an animated cursor

// `rect` cut to `bounds`; all zero when they do not meet.
void CutToBounds(Rect& rect, const Rect& bounds) {
    if (rect.left < bounds.left) rect.left = bounds.left;
    if (rect.top < bounds.top) rect.top = bounds.top;
    if (rect.right > bounds.right) rect.right = bounds.right;
    if (rect.bottom > bounds.bottom) rect.bottom = bounds.bottom;
    if (rect.left >= rect.right || rect.top >= rect.bottom) rect = Rect{};
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
    const std::int32_t size = smallCursors ? kSmallSize : kLargeSize;
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
    const std::uint32_t steps = (platform::Milliseconds() - frameTime) / kFramePeriod;
    image->frame = static_cast<int>(static_cast<unsigned>(image->frame + steps) % static_cast<unsigned>(image->frames));
    frameTime += steps * kFramePeriod;
    bool moved = steps != 0;

    const std::int32_t* state = mouse->Poll();
    motion.x += state[0];
    motion.y += state[1];
    Point point;
    if (held) {
        point = heldPosition;
    } else {
        platform::Point at{};
        platform::CursorPosition(&at);
        point = Point{at.x, at.y};
        heldPosition = point;
    }
    if (point.x < 0) point.x = 0;
    if (point.y < 0) point.y = 0;
    if (point.x > bounds.right) point.x = bounds.right;
    if (point.y > bounds.bottom) point.y = bounds.bottom;
    if (position.x != point.x || position.y != point.y) moved = true;

    ShowFrame(this);
    const std::int32_t left = position.x - image->hotX;
    const std::int32_t top = position.y - image->hotY;
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
