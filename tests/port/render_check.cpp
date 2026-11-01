// The interface canvases' glyph cases (tests/render/glyph_cases.cpp, as tools/ui_check runs them on Direct3D 11) on
// the native renderer: the game's renderer starts itself on its selected backend at the first canvas. Exit code 0
// when all pass.
#include "platform/platform.h"
#include "render/glyph_cases.h"
#include "sokol_gfx.h"

#include <cstdio>
#include <string>

namespace {

int g_checks = 0, g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) ++g_failures;
    std::printf("  %s %s\n", ok ? "ok  " : "FAIL", what.c_str());
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
    glyph_cases::Run(Check);
    sg_commit();
    std::printf("render_check: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
