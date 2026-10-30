// Small application helpers: the quit flag, the movie mode byte, the copy
// protection's delayed check and two EngineObject helpers. See docs/notes/app.md.
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

// The movie mode byte (1 unless the command line changed it; the loop passes it to the frame step).
std::uint8_t g_movieMode = /*00784752*/ 1;

} // namespace app

World* EngineObject::OwningWorld() const {
    auto* context = owner->context;
    return context ? context->ThisWorld() : nullptr;
}
