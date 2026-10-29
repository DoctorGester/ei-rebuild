// The two worlds (g_serverWorld, g_clientWorld) and their constructors: CWorldServer and CWorldClient. Only
// their static initialisers (defined here on the names) call them. The members with C++ constructors (the
// strings, the zone header) are the compiler's; the others' constructors are written out in the original's
// order (the ones written out in the constructors) or called (the out-of-line ones).
#include "world.h"

#include "ai_map.h"
#include "core_streams.h"
#include "effects.h"
#include "ground_print.h"
#include "object_diplomacy.h"
#include "registry.h"
#include "script.h"
#include "spell.h"         // CSpellEffect: the missiles
#include "texture.h"       // CTexture: the fog texture (ClientArrayManager)

#include "lib/static_object.h"

#include <cstdlib>
#include <initializer_list>
#include <new>

namespace {

// The lists' block size (10).
constexpr int kListBlockSize = 10;
// The client's flags after construction.
constexpr std::uint32_t kClientFlags = 0x3DC0;

} // namespace

CWorldServer::CWorldServer() {
    diplomacy_.Reset();
    ResetState();
}

CWorldClient::CWorldClient() {
    dynamicMaxId_ = 0;
    lastLineListKey_ = 0;
    // Both sector grids at 1 x 1 (Create makes them 4 x 4).
    objectGrid_.allocatedWidth_ = 1;
    objectGrid_.allocatedHeight_ = 1;
    objectGrid_.Allocate();
    secondGrid_.allocatedWidth_ = 1;
    secondGrid_.allocatedHeight_ = 1;
    secondGrid_.Allocate();
    handle_.ptr = nullptr;
    flags_ |= kClientFlags;
    worldValue_ = -1;
    camera_ = nullptr;
    materialManager_ = nullptr;
    particleManager_ = nullptr;
    textureManager_ = nullptr;
    lightFactory_ = nullptr;
    listManager_ = nullptr;
    arrayManager_ = nullptr;
    sounds_ = nullptr;
    texts_ = nullptr;
    terrain_ = nullptr;
    session_ = nullptr;
    skyDome_ = nullptr;
    bird_ = nullptr;
    cursor_ = nullptr;
    appName_ = "EvilIslands";
    windowClass_ = "EvilIslandsClass";
    windowTitle_ = "EvilIslands";
    pausedTitle_ = "EvilIslands Paused";
}

// ---- The two worlds -------------------------------------------------------------------------------

// Zero at the start: built by their static initialisers in the list's order and destroyed by the
// exit functions those register; all defined on the names
// (docs/notes/standalone.md, "The static initialisers' objects").
constinit lib::StaticObject<CWorldServer> g_serverWorld = /*007AFF60*/ {};
constinit lib::StaticObject<CWorldClient> g_clientWorld = /*007AF5B8*/ {};

// A second block: it uses g_serverWorld and g_clientWorld (defined above).
namespace {
void DestroyServerWorld() { g_serverWorld.Destroy(); }
void DestroyClientWorld() { g_clientWorld.Destroy(); }
} // namespace

void ConstructServerWorld() {
    g_serverWorld.Construct();
    std::atexit(DestroyServerWorld);
}
void ConstructClientWorld() {
    g_clientWorld.Construct();
    std::atexit(DestroyClientWorld);
}
