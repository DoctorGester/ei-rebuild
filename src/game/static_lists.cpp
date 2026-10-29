// Static initialisers' list constructors (static_lists.h).
#include "static_lists.h"

#include "app_helpers.h"
#include "world.h"

#include <cstdint>
#include <cstdlib>
#include <list>
#include <memory>

// The two lists, empty at the start: each is built by its static initialiser in the list's order and
// destroyed by the exit function the initialiser registers with the game's atexit (docs/notes/standalone.md,
// "The static initialisers' objects"). The copy protection's check list (app_helpers.h) and the frame's deferred
// unit list (world.h).
constinit lib::StaticObject<std::list<std::uint8_t>> g_protectionChecks = /*0079BC2C*/ {};
constinit lib::StaticObject<std::list<HeldRef<CMapObject>>> g_deferredUnits = /*007AF5B4*/ {};

namespace static_lists {

void ConstructDeferredUnits() { g_deferredUnits.Construct(); }

namespace {
void DestroyProtectionChecks() { g_protectionChecks.Destroy(); }
void DestroyDeferredUnits() { g_deferredUnits.Destroy(); }
} // namespace

void ConstructProtectionChecksList() {
    app::ConstructProtectionChecks();
    std::atexit(DestroyProtectionChecks);
}
void ConstructDeferredUnitsList() {
    ConstructDeferredUnits();
    std::atexit(DestroyDeferredUnits);
}

} // namespace static_lists
