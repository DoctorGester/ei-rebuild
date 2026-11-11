// The game window in the switch-over without DirectDraw (present_window.h).
#include "game/app/present_window.h"

#include "engine/scene/device.h"
#include "engine/scene/scene_renderer.h"
#include "game/app/singletons.h"

#include "game/app/app.h"

#include "platform/log.h"
#include "platform/platform.h"
#include "render/render.h"

#include <algorithm>
#include <optional>

namespace present_window {
namespace {

int g_clientWidth = 0, g_clientHeight = 0;   // the window's client area at the last present (0: none yet)

platform::NativeWindow Window() {
    TextureDevice* device = g_sceneRenderer.device;
    return device ? device->window : nullptr;
}

// The cursor's screen position as a point of the frame (the game's screen).
Point ScreenToGame(Point point) {
    platform::NativeWindow window = Window();
    if (!window) return point;
    return render::WindowToFrame(platform::ScreenToClient(window, point));
}

// A point of the frame as the screen's.
Point GameToScreen(Point point) {
    platform::NativeWindow window = Window();
    if (!window) return point;
    return platform::ClientToScreen(window, render::FrameToWindow(point));
}

} // namespace

void Update(TextureDevice* device) {
    if (!device) return;
    platform::MapCursorPoints(ScreenToGame, GameToScreen);
    if (device->fullScreen && device->window)
        if (const std::optional<platform::MonitorCover> cover = platform::CoverMonitor(device->window)) {
            const Rect& was = cover->was;
            const Rect& monitor = cover->monitor;
            ei::Log("present: full screen without a display mode change: the window (%d,%d)-(%d,%d) covers the "
                    "monitor (%d,%d)-(%d,%d)",
                    was.left, was.top, was.right, was.bottom, monitor.left, monitor.top, monitor.right, monitor.bottom);
        }
    // Full screen keeps the resolution of the settings, scaled over the monitor; a window renders at its size once
    // it changes (the size it was made at may be smaller than the settings': the frame is scaled into it).
    const std::optional<Size> client = device->window ? platform::WindowPixelSize(device->window) : std::nullopt;
    if (!client) return;   // (minimised)
    const int width = client->w, height = client->h;
    const bool changed = g_clientWidth != 0 && (width != g_clientWidth || height != g_clientHeight);
    g_clientWidth = width;
    g_clientHeight = height;
    if (changed && !device->fullScreen) app::RequestResolution(width, height);
}

void Present(TextureDevice* device) {
    Update(device);
    render::Present();
}

void SizeClient(TextureDevice* device, int width, int height) {
    if (!device || !device->window || device->fullScreen) return;
    platform::SetClientSize(device->window, width, height);
    const std::optional<Size> client = platform::WindowPixelSize(device->window);
    g_clientWidth = client ? client->w : 0;
    g_clientHeight = client ? client->h : 0;
    ei::Log("present: the window's client area sized to %dx%d (asked %dx%d)", g_clientWidth, g_clientHeight, width,
            height);
}

void SetFullScreen(TextureDevice* device, bool fullScreen) {
    if (!device || !device->window || device->fullScreen == fullScreen) return;
    ei::Log("present: %s", fullScreen ? "full screen without a display mode change" : "a window");
    device->fullScreen = fullScreen;
    platform::SetFullScreen(device->window, fullScreen);
    SizeClient(device, device->screen.right, device->screen.bottom);
}

bool MouseToGame(platform::Event* event) {
    using platform::EventType;
    if (event->type != EventType::MouseMove && event->type != EventType::MouseDown &&
        event->type != EventType::MouseUp && event->type != EventType::Wheel)
        return true;
    Point p = render::WindowToFrame({event->x, event->y});
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
