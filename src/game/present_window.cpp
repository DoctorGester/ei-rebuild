// The game window in the switch-over without DirectDraw (present_window.h).
#include "present_window.h"

#include "texture.h"
#include "game_singletons.h"
#include "model.h"

#include "app.h"

#include "../platform/log.h"
#include "platform/platform.h"
#include "render/render.h"

#include <algorithm>

namespace present_window {
namespace {

int g_clientWidth = 0, g_clientHeight = 0;   // the window's client area at the last present (0: none yet)

platform::NativeWindow Window() {
    TextureDevice* device = g_sceneRenderer.device;
    return device ? device->window : nullptr;
}

// The cursor's screen position as a point of the frame (the game's screen).
void ScreenToGame(Point* point) {
    platform::NativeWindow window = Window();
    if (!window) return;
    platform::ScreenToClient(window, point);
    render::WindowToFrame(&point->x, &point->y);
}

// A point of the frame as the screen's.
void GameToScreen(Point* point) {
    platform::NativeWindow window = Window();
    if (!window) return;
    render::FrameToWindow(&point->x, &point->y);
    platform::ClientToScreen(window, point);
}

} // namespace

void Update(TextureDevice* device) {
    if (!device) return;
    platform::MapCursorPoints(ScreenToGame, GameToScreen);
    Rect was{}, monitor{};
    if (device->fullScreen && device->window && platform::CoverMonitor(device->window, &was, &monitor)) {
        ei::Log("present: full screen without a display mode change: the window (%d,%d)-(%d,%d) covers the monitor "
                "(%d,%d)-(%d,%d)",
                was.left, was.top, was.right, was.bottom, monitor.left, monitor.top, monitor.right, monitor.bottom);
    }
    // Full screen keeps the resolution of the settings, scaled over the monitor; a window renders at its size once
    // it changes (the size it was made at may be smaller than the settings': the frame is scaled into it).
    int width = 0, height = 0;
    if (!device->window || !platform::WindowPixelSize(device->window, &width, &height)) return;   // (minimised)
    const bool changed = g_clientWidth != 0 && (width != g_clientWidth || height != g_clientHeight);
    g_clientWidth = width;
    g_clientHeight = height;
    if (changed && !device->fullScreen) app::RequestResolution(width, height);
}

void SizeClient(TextureDevice* device, int width, int height) {
    if (!device || !device->window || device->fullScreen) return;
    platform::SetClientSize(device->window, width, height);
    if (!platform::WindowPixelSize(device->window, &g_clientWidth, &g_clientHeight)) g_clientWidth = g_clientHeight = 0;
    ei::Log("present: the window's client area sized to %dx%d (asked %dx%d)", g_clientWidth, g_clientHeight, width,
            height);
}

bool MouseToGame(platform::Event* event) {
    using platform::EventType;
    if (event->type != EventType::MouseMove && event->type != EventType::MouseDown &&
        event->type != EventType::MouseUp && event->type != EventType::Wheel)
        return true;
    Point p{event->x, event->y};
    render::WindowToFrame(&p.x, &p.y);
    bool onFrame = true;
    if (const TextureDevice* device = g_sceneRenderer.device) {
        const Point clamped = Min(Max(p, Point{}), device->screen.BottomRight() - Point{1, 1});
        onFrame = clamped == p;
        p = clamped;
    }
    event->x = p.x;
    event->y = p.y;
    return onFrame;
}

} // namespace present_window
