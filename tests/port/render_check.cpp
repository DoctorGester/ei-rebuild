// The interface canvases' glyph cases (tests/render/glyph_cases.cpp, as tools/ui_check runs them on Direct3D 11) on
// the native renderer: the game's renderer starts itself on its selected backend at the first canvas. Exit code 0
// when all pass.
#include "game/ui_cursor.h"
#include "platform/platform.h"
#include "render/glyph_cases.h"
#include "sokol_gfx.h"

#include <SDL3/SDL.h>
#include <cstdio>
#include <string>

namespace {

int g_checks = 0, g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
}

// Motion pending before a press is discarded; motion after it survives repeated held-state updates.
void CheckCursorMotion(platform::NativeWindow window, int width, int height) {
    auto move = [&] {
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = SDL_GetWindowID(static_cast<SDL_Window*>(window));
        motion.motion.x = 20.0f;
        motion.motion.y = 30.0f;
        motion.motion.xrel = 5.0f;
        motion.motion.yrel = -3.0f;
        SDL_PushEvent(&motion);
        platform::Event event;
        while (platform::TakeEvent(&event)) {
        }
    };
    DirectInputMouse mouse;
    Cursor cursor;
    cursor.enabled = true;
    cursor.mouse = &mouse;
    cursor.bounds = Rect{0, 0, width, height};
    cursor.motion = Point{9, -4};
    platform::MapCursorPoints([](platform::Point* at) { *at = {31, 47}; }, nullptr);
    move();
    cursor.SetHeld(true);
    const std::int32_t* state = mouse.Poll();
    Check(cursor.motion.x == 0 && cursor.motion.y == 0 && state[0] == 0 && state[1] == 0,
          "holding discards movement from before the press");
    platform::MapCursorPoints([](platform::Point* at) { *at = {83, 96}; }, nullptr);
    move();
    cursor.SetHeld(true);
    state = mouse.Poll();
    Check(state[0] > 0 && state[1] < 0, "holding again keeps movement from after the press");
    Check(cursor.position.x == 31 && cursor.position.y == 47 && cursor.heldPosition.x == 31 &&
              cursor.heldPosition.y == 47,
          "holding keeps the pointer position captured on the press");
    cursor.SetHeld(false);
    platform::MapCursorPoints(nullptr, nullptr);
}

} // namespace

int main() {
    platform::RegisterWindowClass(nullptr, "ei-render-check", nullptr);
    platform::NativeWindow window = platform::CreateGameWindow(nullptr, "Render check", 640, 400, false);
    int width = 0, height = 0;
    Check(window && platform::WindowPixelSize(window, &width, &height) && width == 640 && height == 400,
          "window drawable matches the configured pixel size");
    platform::Point point{73, 51};
    platform::ClientToScreen(window, &point);
    platform::ScreenToClient(window, &point);
    Check(point.x == 73 && point.y == 51, "client and screen pixel coordinates round-trip");
    if (window) CheckCursorMotion(window, width, height);
    glyph_cases::Run(Check);
    sg_commit();
    std::printf("render_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
