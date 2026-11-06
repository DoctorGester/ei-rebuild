// The rendering resolution changed while the game runs (app.h's SetResolution): a window resized by the player or
// the system, or a resolution chosen in the options. docs/notes/renderer.md, "Resolution changes".
#include "game/app.h"

#include "game/game_singletons.h"
#include "game/device.h"
#include "game/model.h"
#include "game/options.h"
#include "game/present_window.h"
#include "game/screen_layout.h"
#include "game/ui.h"
#include "game/ui_cursor.h"

#include "platform/log.h"

namespace app {

namespace {

constexpr int kLargestSide = 16384;   // the renderer's frame target's (render::GameTargetSize)

int g_requestedWidth = 0, g_requestedHeight = 0;   // RequestResolution's, taken by the next pass (0: none)

} // namespace

bool SetResolution(int width, int height) {
    TextureDevice* device = g_sceneRenderer.device;
    if (!device || width < 1 || height < 1 || width > kLargestSide || height > kLargestSide) return false;
    const Rect& screen = device->screen;
    if (screen.right == width && screen.bottom == height) return false;
    ei::Log("Resolution: %dx%d (was %dx%d)", width, height, screen.right, screen.bottom);
    const screen_layout::Layout before = screen_layout::Get();
    screen_layout::Configure(width, height);
    g_sceneRenderer.ResizeScreen(width, height);
    g_cursor.bounds = device->screen;
    options::g_options.resolutionX = width;
    options::g_options.resolutionY = height;
    if (g_interface.renderer) g_interface.Resize(before);
    return true;
}

void RequestResolution(int width, int height) {
    g_requestedWidth = width;
    g_requestedHeight = height;
}

void ChooseResolution(int width, int height) {
    RequestResolution(width, height);
    present_window::SizeClient(g_sceneRenderer.device, width, height);
}

void ChooseFullScreen(bool fullScreen) {
    options::g_options.fullScreen = fullScreen ? 1 : 0;
    present_window::SetFullScreen(g_sceneRenderer.device, fullScreen);
}

void TakeResolutionChange() {
    if (!g_requestedWidth) return;
    SetResolution(g_requestedWidth, g_requestedHeight);
    g_requestedWidth = g_requestedHeight = 0;
}

} // namespace app
