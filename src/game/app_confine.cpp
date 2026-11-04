// The cursor confined to the game window (app.h's UpdateCursorConfinement). docs/notes/platform.md, "The cursor".
#include "app.h"

#include "game_singletons.h"
#include "options.h"
#include "ui.h"

#include "platform/log.h"
#include "platform/platform.h"

namespace app {

namespace {

bool g_confined = false;   // what the window was last asked

} // namespace

void UpdateCursorConfinement() {
    const bool optionOn = options::g_options.confineCursor != 0;
    const bool confined = optionOn && g_active != 0;
    // Focus lost with the option on: the cursor goes where it likes, and an edge pan the last motion started must not
    // go on for it.
    if (optionOn && !confined && g_confined) g_interface.ResetInput();
    if (confined == g_confined) return;
    g_confined = confined;
    platform::ConfineCursor(g_mainWindow, confined);
    ei::Log("app: cursor %s the window", confined ? "confined to" : "released from");
}

} // namespace app
