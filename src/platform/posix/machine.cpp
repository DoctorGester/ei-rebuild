// The platform's machine on Linux and macOS (platform.h, "The machine"): text smoothing (the desktop smooths), the
// primary display's size (SDL's) and the environment.
#include "platform/platform.h"

#include "platform/seams.h"

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdlib>

namespace platform {

bool FontSmoothing() { return !IsolatedRun(); }

void PrimaryDesktopSize(int* width, int* height) {
    SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay())) {
#if defined(__APPLE__)
        // In pixels, as the game's sizes are (a Retina display's points times its density; platform_sdl.cpp's
        // CreateGameWindow makes the window in points).
        *width = static_cast<int>(std::lround(mode->w * mode->pixel_density));
        *height = static_cast<int>(std::lround(mode->h * mode->pixel_density));
#else
        *width = mode->w;
        *height = mode->h;
#endif
    }
}

std::string EnvironmentVariable(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

} // namespace platform
