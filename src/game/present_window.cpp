// The game window in the switch-over without DirectDraw (present_window.h).
#include "present_window.h"

#include "texture.h"
#include "game_singletons.h"
#include "model.h"

#include "../hook/log.h"
#include "platform/platform.h"
#include "render/render.h"

namespace present_window {
namespace {

platform::NativeWindow Window() {
    TextureDevice* device = g_sceneRenderer.device;
    return device ? device->window : nullptr;
}

// The frame does not fill the client area one to one: the game's coordinates need mapping.
bool Scaled() {
    int fw = 0, fh = 0;
    render::Rect placed{};
    if (!render::FrameInWindow(&fw, &fh, &placed)) return false;
    return placed.left != 0 || placed.top != 0 || placed.right != fw || placed.bottom != fh;
}

// The cursor's screen position as a point of the frame (the game's screen).
void ScreenToGame(platform::Point* point) {
    platform::NativeWindow window = Window();
    if (!window || !Scaled()) return;
    platform::ScreenToClient(window, point);
    render::WindowToFrame(&point->x, &point->y);
}

// A point of the frame as the screen's.
void GameToScreen(platform::Point* point) {
    platform::NativeWindow window = Window();
    if (!window || !Scaled()) return;
    render::FrameToWindow(&point->x, &point->y);
    platform::ClientToScreen(window, point);
}

} // namespace

void Update(TextureDevice* device) {
    if (!device) return;
    platform::MapCursorPoints(ScreenToGame, GameToScreen);
    platform::Rect was{}, monitor{};
    if (device->fullScreen && device->window && platform::CoverMonitor(device->window, &was, &monitor)) {
        ei::Log("present: full screen without a display mode change: the window (%d,%d)-(%d,%d) covers the monitor "
                "(%d,%d)-(%d,%d)",
                was.left, was.top, was.right, was.bottom, monitor.left, monitor.top, monitor.right, monitor.bottom);
    }
}

void MouseToGame(platform::Event* event) {
    using platform::EventType;
    const bool client = event->type >= EventType::MouseMove && event->type <= EventType::RightDouble;
    if (!(client || event->type == EventType::Wheel) || !Scaled()) return;
    platform::Point p{event->x, event->y};
    if (event->type == EventType::Wheel) {
        platform::NativeWindow window = Window();
        if (!window) return;
        platform::ScreenToClient(window, &p);
    }
    render::WindowToFrame(&p.x, &p.y);
    event->x = p.x;
    event->y = p.y;
}

} // namespace present_window
