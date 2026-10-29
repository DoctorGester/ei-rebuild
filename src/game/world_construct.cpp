// The two worlds (g_serverWorld, g_clientWorld) and their constructors: WorldServer and WorldClient. The
// members with C++ constructors (the strings, the zone header) are the compiler's; the others' constructors are
// written out in the original's order (the ones written out in the constructors) or called (the out-of-line ones).
#include "world.h"

#include "ai_map.h"
#include "core_streams.h"
#include "effects.h"
#include "ground_print.h"
#include "object_diplomacy.h"
#include "registry.h"
#include "script.h"
#include "spell.h"         // SpellEffect: the missiles
#include "texture.h"       // CTexture: the fog texture (ClientArrayManager)

#include <initializer_list>
#include <new>

namespace {

// The client's flags after construction: every draw but the shadows (Create decides them).
constexpr WorldClientFlag kClientFlags = WorldClientFlag::DrawObjects | WorldClientFlag::DrawUnits | WorldClientFlag::DrawTerrain |
                                         WorldClientFlag::DrawEffects | WorldClientFlag::Draw800 | WorldClientFlag::DrawSky |
                                         WorldClientFlag::DrawNumbers;
static_assert(lib::Bits(kClientFlags) == 0x3DC0);

} // namespace

WorldServer::WorldServer() {
    diplomacy.Reset();
    ResetState();
}

WorldClient::WorldClient() {
    dynamicMaxId = 0;
    lastLineListKey = 0;
    // Both sector grids at 1 x 1 (Create makes them 4 x 4).
    objectGrid.allocatedWidth = 1;
    objectGrid.allocatedHeight = 1;
    objectGrid.Allocate();
    secondGrid.allocatedWidth = 1;
    secondGrid.allocatedHeight = 1;
    secondGrid.Allocate();
    flags |= kClientFlags;
    worldValue = -1;
    camera = nullptr;
    materialManager = nullptr;
    particleManager = nullptr;
    textureManager = nullptr;
    lightFactory = nullptr;
    listManager = nullptr;
    arrayManager = nullptr;
    sounds = nullptr;
    texts = nullptr;
    terrain = nullptr;
    session = nullptr;
    skyDome = nullptr;
    bird = nullptr;
    cursor = nullptr;
    appName = "EvilIslands";
    windowClass = "EvilIslandsClass";
    windowTitle = "EvilIslands";
    pausedTitle = "EvilIslands Paused";
}

// ---- The two worlds -------------------------------------------------------------------------------

// Constructed by the C++ runtime (the client first, as the original's static initialisers did) and destroyed
// after the game's exit functions; GameMain empties them at exit before those run (EmptyWorlds, app_main.cpp), and
// their destructors rely on that.
WorldClient g_clientWorld;
WorldServer g_serverWorld;
