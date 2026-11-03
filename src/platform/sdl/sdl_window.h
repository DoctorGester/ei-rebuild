#pragma once

#include "platform/platform.h"

struct SDL_Window;

namespace platform {

// SDL's window for the game's native handle (an HWND on Windows); null for another window.
SDL_Window* SdlWindow(NativeWindow window);

}   // namespace platform
