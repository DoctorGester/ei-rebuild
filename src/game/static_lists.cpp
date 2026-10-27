// Static initialisers' list constructors (static_lists.h).
#include "static_lists.h"

#include "app_helpers.h"
#include "texture.h"
#include "world.h"

#include "ei/hook.h"

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
using AtExitFunction = void(__cdecl*)();
} // namespace

namespace static_lists {

void __stdcall ConstructDirectDrawDrivers() { g_directDrawDrivers->Construct(); }

void __stdcall ConstructDeferredUnits() { g_deferredUnits->Construct(); }

namespace {
void __cdecl ConstructProtectionChecksList() {
    app::ConstructProtectionChecks();
    libcrt::AtExit(ei::Code<AtExitFunction>(0x00455F00));
}
void __cdecl DestroyProtectionChecks() {
    stlp::ListAbi<std::uint8_t>::Destruct(&g_protectionChecks.Get());
}
void __cdecl ConstructDriversList() {
    ConstructDirectDrawDrivers();
    libcrt::AtExit(ei::Code<AtExitFunction>(0x004ED5F0));
}
void __cdecl DestroyDrivers() {
    if (g_driversDestroyed & 1) return;
    g_driversDestroyed = static_cast<std::uint8_t>(g_driversDestroyed | 1);
    stlp::ListAbi<DirectDrawDriver>::Destruct(&g_directDrawDrivers.Get());
}
void __cdecl ConstructDeferredUnitsList() {
    ConstructDeferredUnits();
    libcrt::AtExit(ei::Code<AtExitFunction>(0x0056A4C0));
}
void __cdecl DestroyDeferredUnits() { SectorListDestruct(&g_deferredUnits.Get()); }
} // namespace

} // namespace static_lists

EI_REWRITTEN(0x004ED540, static_lists::ConstructDirectDrawDrivers);
EI_REWRITTEN(0x0056A410, static_lists::ConstructDeferredUnits);
EI_REPLACE(0x00455E30, static_lists::ConstructProtectionChecksList);
EI_REPLACE(0x00455F00, static_lists::DestroyProtectionChecks);
EI_REPLACE(0x004ED520, static_lists::ConstructDriversList);
EI_REPLACE(0x004ED5F0, static_lists::DestroyDrivers);
EI_REPLACE(0x0056A3F0, static_lists::ConstructDeferredUnitsList);
EI_REPLACE(0x0056A4C0, static_lists::DestroyDeferredUnits);

// ~list() of the lists only exit functions destroy: every node's element destroyed and the node returned to
// the pool, then the sentinel. The copy protection's check list (bytes), and the DirectDraw driver list (each
// driver's description, then its name; the guarded exit function).
EI_REWRITTEN(0x004584E0, stlp::ListAbi<std::uint8_t>::Destruct);
EI_REWRITTEN(0x004F0520, stlp::ListAbi<DirectDrawDriver>::Destruct);
