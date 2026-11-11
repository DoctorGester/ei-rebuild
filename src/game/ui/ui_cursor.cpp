// The cursor images' constructor (the cursor object, see docs/notes/ui.md, "Cursor"), which builds each image's mask
// from its picture's pixels in the .mmp file (the holder has no memory). (The software cursor's blit, which combined
// image, mask and background through Locks of the holders' surfaces, is gone: the cursor is the system's.)
#include "game/ui/ui_cursor.h"

#include "game/ui/camera_trace.h"
#include "game/app/singletons.h"
#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "engine/models/texture.h"
#include "engine/scene/surface.h"
#include "game/world/world.h"
#include "lib/crt.h"
#include "platform/platform.h"
#include "render/render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

// --- a cursor image ------------------------------------------------------------------------------

namespace {

// The mask of the 8888 picture at 32 bits a pixel, its rows one after another: all ones where the pixel's colour in
// the screen's format is 0 (its low 24 bits), else 0.
void BuildMask(const MmpFile& picture, std::uint8_t* mask) {
    ChannelLayout layout;
    layout.Split(ScreenFormat());
    const std::size_t bytes = 4;
    const std::uint8_t* pixels = picture.Body() + picture.header.MipOffset(0);
    const std::size_t count = static_cast<std::size_t>(picture.header.width) * picture.header.height;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t argb;
        std::memcpy(&argb, pixels + i * 4, sizeof(argb));
        std::memset(mask + i * bytes, layout.ToNative(argb) == 0 ? 0xFF : 0, bytes);
    }
}

constexpr std::int32_t kLargeSize = 0x20;
constexpr std::int32_t kSmallSize = 0x10;

constexpr std::uint32_t kFramePeriod = 125;         // ms per frame of an animated cursor

// A holder pointer emptied, then its old holder released.
void ReleaseHolder(Surface*& slot) {
    Surface* old = slot;
    slot = nullptr;
    if (old) old->Release();
}

// The current image's current frame as the part shown.
void ShowFrame(Cursor* cursor) {
    const CursorImage* image = cursor->image;
    const int left = image->frame * image->width;
    cursor->from = {left, 0, left + image->width, image->height};
}

struct ShapeFiles {
    const char* name = nullptr;
    const char* smallName = nullptr;
    int hotX = 0, hotY = 0;
};
// In the shape order the game selects them by.
const ShapeFiles kShapes[] = {
    {"cursor_default", "sm_cursor_default", 0, 0},
    {"cursor_attack", "sm_cursor_attack", 0, 0},
    {"cursor_spell", "sm_cursor_spell", 0, 0},
    {"cursor_wait", "sm_cursor_wait", 0, 0},
    {"cursor_use", "sm_cursor_use", 0, 0},
    {"cursor_scroll_u", "sm_cursor_scroll_u", 16, 0},
    {"cursor_scroll_ur", "sm_cursor_scroll_ur", 32, 0},
    {"cursor_scroll_r", "sm_cursor_scroll_r", 32, 16},
    {"cursor_scroll_dr", "sm_cursor_scroll_dr", 32, 32},
    {"cursor_scroll_d", "sm_cursor_scroll_d", 16, 32},
    {"cursor_scroll_dl", "sm_cursor_scroll_dl", 0, 32},
    {"cursor_scroll_l", "sm_cursor_scroll_l", 0, 16},
    {"cursor_scroll_ul", "sm_cursor_scroll_ul", 0, 0},
    {"cursor_cancel", "sm_cursor_cancel", 0, 0},
    {"cursor_talk", "sm_cursor_talk", 0, 0},
    {"cursor_move", "sm_cursor_move", 0, 0},
    {"cursor_attack_hd", "sm_cursor_attack_hd", 0, 0},
    {"cursor_attack_bd", "sm_cursor_attack_bd", 0, 0},
    {"cursor_attack_lh", "sm_cursor_attack_lh", 0, 0},
    {"cursor_attack_rh", "sm_cursor_attack_rh", 0, 0},
    {"cursor_attack_ll", "sm_cursor_attack_ll", 0, 0},
    {"cursor_attack_rl", "sm_cursor_attack_rl", 0, 0},
    {"cursor_camera", "sm_cursor_camera", 0, 0},
    {"cursor_spellcancel", "sm_cursor_spellcancel", 0, 0},
    {"cursor_steal", "sm_cursor_steal", 0, 0},
};

} // namespace

CursorImage::CursorImage(std::string_view name, int hotSpotX, int hotSpotY) {
    TextureManager* manager = g_clientWorld.textureManager.get();
    const MmpFile picture = manager->GetFileData(name);
    // (A throw passes the exception on; that ends the game.)
    holder = new Surface(g_sceneRenderer.device, picture);
    holder->AddRef();

    // The holder took the picture's size and the screen's pixel size or threw.
    std::uint32_t pictureWidth = static_cast<std::uint32_t>(holder->width), pictureHeight = static_cast<std::uint32_t>(holder->height);
    std::uint32_t count = pictureWidth / pictureHeight;
    height = static_cast<std::int32_t>(pictureHeight);
    frames = static_cast<int>(count);
    width = static_cast<std::int32_t>(pictureWidth / count);
    hot = {static_cast<int>(g_cursorWidth) * hotSpotX / 32, static_cast<int>(g_cursorHeight) * hotSpotY / 32};
    std::uint32_t maskBytes = (static_cast<std::uint32_t>(holder->bitsPerPixel) * pictureHeight * pictureWidth) >> 3;
    saved.assign(maskBytes, 0);
    BuildMask(picture, saved.data());
    manager->CloseFile(picture.data);
}

// --- Frame -------------------------------------------------------------------------------------------

// The cursor in the frame: the image's current frame with its mask and hot spot goes to the renderer (render::Cursor),
// whose present shows it as the system's cursor (held still while the game holds it); a frame that hands none over
// (a movie, a loading screen, the image hidden) hides it. The original's cursor thread, which redrew the cursor on
// the primary between frames, and the DirectDraw background saves are gone: the system moves the cursor. See
// docs/notes/ui.md, "Cursor", and docs/notes/renderer.md section 9.

void CursorDrawFrame(Cursor* self) {
    if (!g_sceneRenderer.running) return;
    if (!self->enabled) return;
    self->UpdatePosition();
    if (!self->imageShown) return;
    const CursorImage* image = self->image;
    render::FrameCursor frame;
    frame.imageHolder = image->holder;
    frame.mask = image->saved.data();
    frame.from = Rect{image->frame * image->width, 0, (image->frame + 1) * image->width, image->height};
    frame.hot = image->hot;
    frame.held = self->held;
    frame.heldAt = self->position;
    render::Cursor(frame);
    self->drawn = self->rect;
}

// --- The cursor's own methods ------------------------------------------------------------------------

// The cursor (Cursor, ui_cursor.h): its set-up Init AddShape (called by SetUpCursors, below, at start and after a
// display reset), its life cycle, shape and the position update each frame runs. See docs/notes/ui.md, section 3.

void Cursor::Init(TextureDevice* newDevice, bool smallCursors) {
    const std::int32_t size = smallCursors ? kSmallSize : kLargeSize;
    g_cursorWidth = size;
    g_cursorHeight = size;
    position = {};
    motion = {};
    image = nullptr;
    shape = -1;
    imageShown = true;
    enabled = true;
    TextureDevice* old = device;
    device = newDevice;
    if (newDevice) newDevice->AddRef();
    if (old) old->Release();
    bounds = device->screen;
}

void Cursor::AddShape(std::string_view name, int hotX, int hotY) {
    images.push_back(new CursorImage(std::string(name) + ".mmp", hotX, hotY));
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

int Cursor::SetShape(CursorShape newShape) {
    const int previous = shape;
    if (previous == -1 || !enabled) return -1;
    shape = static_cast<int>(newShape);
    image = images[shape];
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
        platform::TakeMouseMotion();   // the motion from before the press dropped
        motion = {};
        pollTime = platform::FrameClock();
        pollCount = 0;
        dragGiven = true;
        if (const std::optional<Point> at = platform::CursorPosition()) {
            heldPosition = Min(Max(*at, Point{}), bounds.BottomRight());
            position = heldPosition;
        }
    }
    held = newHeld;
}

Point Cursor::TakeDrag() {
    if (dragGiven) return {};
    dragGiven = true;
    return drag;
}

Point Cursor::DragOfPolls() const {
    const float window = std::max(kDragFrame, static_cast<float>(polls[newestPoll].interval));
    float covered = 0.0f, x = 0.0f, y = 0.0f;
    for (int i = 0; i < pollCount && covered < window; ++i) {
        const Poll& poll = polls[(newestPoll - i + kPollsKept) % kPollsKept];
        const float span = static_cast<float>(poll.interval);
        // The oldest poll in the window counts for its part in it.
        const float part = covered + span <= window ? 1.0f : (window - covered) / span;
        x += part * static_cast<float>(poll.motion.x);
        y += part * static_cast<float>(poll.motion.y);
        covered += part * span;
    }
    const float scale = kDragFrame / window;
    return Point{static_cast<int>(std::lround(x * scale)), static_cast<int>(std::lround(y * scale))};
}

void Cursor::Start() {
    shape = 0;
    if (!enabled) return;
    image = images[0];
    ShowFrame(this);
}

void Cursor::Shutdown() {
    for (CursorImage* entry : images) {
        ReleaseHolder(entry->holder);
        delete entry;
    }
    images.clear();
    TextureDevice* currentDevice = device;
    device = nullptr;
    if (currentDevice) currentDevice->Release();
    position = {};
    motion = {};
    image = nullptr;
    shape = -1;
    enabled = false;
}

bool Cursor::UpdatePosition() {
    // The frame: one step per 125 ms since frameTime (unsigned), wrapped by the frame count.
    const std::uint32_t now = platform::FrameClock();
    const std::uint32_t steps = (now - frameTime) / kFramePeriod;
    image->frame = static_cast<int>(static_cast<unsigned>(image->frame + steps) % static_cast<unsigned>(image->frames));
    frameTime += steps * kFramePeriod;
    bool moved = steps != 0;

    const Point step = platform::TakeMouseMotion();
    motion += step;
    newestPoll = (newestPoll + 1) % kPollsKept;
    polls[newestPoll] = Poll{now - pollTime, step};
    pollCount = std::min(pollCount + 1, kPollsKept);
    pollTime = now;
    drag = DragOfPolls();
    dragGiven = false;
    camera_trace::Poll(now, polls[newestPoll].interval, polls[newestPoll].motion, drag, held);
    Point point;
    if (held) {
        point = heldPosition;
    } else {
        point = platform::CursorPosition().value_or(Point{});
        heldPosition = point;
    }
    point = Min(Max(point, Point{}), bounds.BottomRight());
    if (position != point) moved = true;

    ShowFrame(this);
    const Point corner = position - image->hot;
    rect = Rect{0, 0, from.Width(), from.Height()}.Offsetted(corner).Intersection(bounds);
    const Point origin = from.TopLeft() + rect.TopLeft() - corner;
    from = {origin.x, origin.y, origin.x + rect.Width(), origin.y + rect.Height()};
    position = point;
    return moved;
}

// --- The cursors' set-up and reset ----------------------------------------------------------------

// The mouse cursor's set-up and reset: the cursor object is initialised and given its 25 shapes, the "sm_"
// set when the small cursors are on. The cursor object itself stays the game's own code.
// Set-up. `smallCursors` (low byte tested) goes to the cursor's Init.
void SetUpCursors(int smallCursors) {
    const bool useSmall = static_cast<std::uint8_t>(smallCursors) != 0;
    g_cursor.Init(g_sceneRenderer.device, useSmall);
    for (const ShapeFiles& shape : kShapes)
        g_cursor.AddShape(useSmall ? shape.smallName : shape.name, shape.hotX, shape.hotY);
    g_cursor.Start();
}

void ResetCursors(int smallCursors) {
    g_cursor.Shutdown();   // the shapes released
    SetUpCursors(smallCursors);
}
