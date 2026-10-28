// Static initialisers' list constructors (static_lists.h).
#include "static_lists.h"

#include "app_helpers.h"
#include "texture.h"
#include "world.h"

#include <cstdint>

// The three lists, empty at the start: each is built by its static initialiser in the list's order and
// destroyed by the exit function the initialiser registers with the game's atexit (docs/notes/standalone.md,
// "The static initialisers' objects"). The copy protection's check list (app_helpers.h), the DirectDraw driver
// list (texture.h) and the frame's deferred unit list (world.h).
constinit lib::StaticObject<stlp::list<std::uint8_t>> g_protectionChecks = /*0079BC2C*/ {};
constinit lib::StaticObject<stlp::list<DirectDrawDriver>> g_directDrawDrivers = /*007AEE64*/ {};
constinit lib::StaticObject<stlp::list<CMapObject*>> g_deferredUnits = /*007AF5B4*/ {};

namespace {
constinit std::uint8_t g_driversDestroyed = /*007AEE60*/ 0;   // the driver list's exit function runs once
} // namespace

namespace static_lists {

void __stdcall ConstructDirectDrawDrivers() { g_directDrawDrivers->Construct(); }

void __stdcall ConstructDeferredUnits() { g_deferredUnits->Construct(); }

namespace {
void __cdecl DestroyProtectionChecks() {
    stlp::ListAbi<std::uint8_t>::Destruct(&g_protectionChecks.Get());
}
void __cdecl DestroyDrivers() {
    if (g_driversDestroyed & 1) return;
    g_driversDestroyed = static_cast<std::uint8_t>(g_driversDestroyed | 1);
    stlp::ListAbi<DirectDrawDriver>::Destruct(&g_directDrawDrivers.Get());
}
void __cdecl DestroyDeferredUnits() { SectorListDestruct(&g_deferredUnits.Get()); }
} // namespace

void __cdecl ConstructProtectionChecksList() {
    app::ConstructProtectionChecks();
    libcrt::AtExit(DestroyProtectionChecks);
}
void __cdecl ConstructDriversList() {
    ConstructDirectDrawDrivers();
    libcrt::AtExit(DestroyDrivers);
}
void __cdecl ConstructDeferredUnitsList() {
    ConstructDeferredUnits();
    libcrt::AtExit(DestroyDeferredUnits);
}

} // namespace static_lists
