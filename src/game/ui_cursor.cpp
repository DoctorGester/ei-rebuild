// The cursor images' constructor (the cursor object, see docs/notes/ui.md, "Cursor"), which builds each image's mask
// from its picture's pixels in the .mmp file (the holder has no memory). (The software cursor's blit, which combined
// image, mask and background through Locks of the holders' surfaces, is gone: the cursor is the system's.)
#include "ui_cursor.h"

#include "game_singletons.h"
#include "model.h"
#include "platform/platform.h"
#include "render/render.h"
#include "surface.h"
#include "texture.h"
#include "world.h"

#include "lib/crt.h"

#include <algorithm>
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

// The cursor in the frame: the image's current frame with its mask and hot spot goes to the renderer (render::Cursor),
// whose present shows it as the system's cursor (held still while the game holds it); a frame that hands none over
// (a movie, a loading screen, the image hidden) hides it. The original's cursor thread, which redrew the cursor on
// the primary between frames, and the DirectDraw background saves are gone: the system moves the cursor. See
// docs/notes/ui.md, "Cursor", and docs/notes/renderer.md section 9.

namespace {

// The renderer object; its flags' bit 0 is the software cursor display option.
bool SoftwareCursor() { return (g_sceneRenderer.flags & 1) != 0; }

} // namespace

void CursorDrawFrame(Cursor* self) {
    if (!SoftwareCursor()) return;
    if (!self->enabled) return;
    self->UpdatePosition();
    if (!self->imageShown) return;
    const CursorImage* image = self->image;
    render::FrameCursor frame;
    frame.imageHolder = image->holder;
    frame.mask = image->saved.data();
    frame.from = render::Rect{image->frame * image->width, 0, (image->frame + 1) * image->width, image->height};
    frame.hotX = image->hotX;
    frame.hotY = image->hotY;
    frame.held = self->held;
    frame.heldX = self->position.x;
    frame.heldY = self->position.y;
    render::Cursor(frame);
    self->drawn = self->rect;
}

// --- Setup -------------------------------------------------------------------------------------------

// The cursor (Cursor, ui_cursor.h): its set-up Init AddShape (called by the cursor set-up, cursor_setup.cpp, at
// start and after a display reset), its life cycle, shape and the position update each frame runs. See
// docs/notes/ui.md, section 3.

namespace {

constexpr std::int32_t kLargeSize = 0x20;
constexpr std::int32_t kSmallSize = 0x10;

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

void Cursor::Init(TextureDevice* newDevice, DirectInputMouse* newMouse, bool smallCursors) {
    const std::int32_t size = smallCursors ? kSmallSize : kLargeSize;
    g_cursorWidth = size;
    g_cursorHeight = size;
    position.x = 0;
    motion.x = 0;
    position.y = 0;
    motion.y = 0;
    image = nullptr;
    imageSpare = 0;
    shape = -1;
    imageShown = true;
    enabled = true;
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

// The cursor.
Cursor g_cursor;

int Cursor::SetShape(int newShape) {
    const int previous = shape;
    if (previous == -1 || !enabled) return -1;
    shape = newShape;
    image = images[newShape];
    ShowFrame(this);
    return previous;
}

void Cursor::ShowImage(bool shown) {
    if (!enabled) return;
    imageShown = shown;
}

void Cursor::SetHeld(bool newHeld) {
    if (!enabled) return;
    if (newHeld && !held) {
        // Poll before holding: movement from before the press must not turn the camera on the next frame.
        mouse->Poll();
        motion = Point{};
        platform::Point at{};
        if (platform::CursorPosition(&at)) {
            heldPosition = Point{std::clamp(at.x, 0, bounds.right), std::clamp(at.y, 0, bounds.bottom)};
            position = heldPosition;
        }
    }
    held = newHeld;
}

void Cursor::Start() {
    shape = 0;
    if (!enabled) return;
    image = images[0];
    ShowFrame(this);
}

void Cursor::Shutdown() {
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
    shape = -1;
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
