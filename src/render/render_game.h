#pragma once

// The game globals the new renderer reads, in one place (an offline test can point them at its own
// data). Everything else it reads hangs off these or comes in as arguments. Where() holds the game's
// named globals by default (render.cpp).

#include <windows.h>

class CSceneRenderer;

namespace render::game {

struct Addresses {
    HWND* window;               // the game window (app::g_mainWindow)
    CSceneRenderer* renderer;   // its clear colour, device object and render target
};

Addresses& Where();

} // namespace render::game
