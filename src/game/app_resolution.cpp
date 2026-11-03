// The rendering resolution changed while the game runs (app.h's SetResolution): a window resized by the player or
// the system, later a resolution chosen in the settings. docs/notes/renderer.md, "Resolution changes".
#include "app.h"

#include "game_singletons.h"
#include "model.h"
#include "options.h"
#include "screen_layout.h"
#include "texture.h"
#include "ui.h"
#include "ui_cursor.h"

#include "hook/log.h"

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

void TakeResolutionChange() {
    if (!g_requestedWidth) return;
    SetResolution(g_requestedWidth, g_requestedHeight);
    g_requestedWidth = g_requestedHeight = 0;
}

} // namespace app
