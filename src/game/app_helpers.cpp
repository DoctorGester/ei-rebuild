// Small application helpers: the quit flag and three EngineObject helpers. See docs/notes/app.md.
#include "app_helpers.h"

#include "app.h"
#include "base_manager.h"
#include "camera.h"
#include "engine_object.h"
#include "game_singletons.h"
#include "item.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "options.h"
#include "unit_create.h"   // MapObjectManager
#include "world.h"

#include "platform/platform.h"

#include <cstdint>
#include <memory>

namespace app {

bool QuitGame(int exitCode) {
    if (!g_quitAllowed) return false;
    platform::PostQuit(exitCode);
    return true;
}

} // namespace app
