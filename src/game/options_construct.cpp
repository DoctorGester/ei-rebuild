// The options object's constructor.
// More: docs/notes/app.md, "Options".
#include "game/options.h"

#include <cstdint>

namespace options {

// The strings start empty (the members' own constructors).
GameOptions::GameOptions() {
    workPath = ".\\";
    resourcesPath = ".\\res\\";

    cameraScroll = {1.0f, 1.0f, 0.5f};
    cameraRotate = {1.0f, 1.0f, 0.5f};
    cameraPitch = {1.0f, 1.0f, 0.5f};
    cameraZoom = {1.0f, 1.0f, 0.5f};
    // Every shortcut: the default distance, looking along Z, 45 degrees of pitch.
    for (CameraShortcutOptions& shortcut : cameraShortcuts)
        shortcut = {cameraDefaultDistance, 0.0f, 0.0f, 1.0f, 0.0f, 45.0f};
}

// The game's options: constructed by the C++ runtime before the static initialisers and WinMain (Load fills them),
// destroyed after the game's exit functions (ShutDown's Save has written them back).
GameOptions g_options;

} // namespace options
