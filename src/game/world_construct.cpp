// The two worlds (g_serverWorld, g_clientWorld) and their constructors: CWorldServer and CWorldClient. The
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
#include "spell.h"         // CSpellEffect: the missiles
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

// Constructed by the C++ runtime (the client first, as the original's static initialisers did) and destroyed
// after the game's exit functions; GameMain empties them at exit before those run (EmptyWorlds, app_main.cpp).
CWorldClient g_clientWorld;
CWorldServer g_serverWorld;
