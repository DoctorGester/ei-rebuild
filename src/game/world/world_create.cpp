// The world's managers created (the server's and the client's world both call it, and it is a virtual
// of their base). See docs/notes/combat.md ("World managers").
// The world's paths are taken from the command line globals; every manager is made and attached
// to the world (ThisWorld with the world, or the base); a helper goes into a
// counted pointer (worldHandle); then the ISAAC generator is seeded and countdowns set up.
#include "game/items/item_manager.h"

#include "game/ai/ai_map.h"
#include "game/core/base_manager.h"
#include "game/core/core_refcount.h"
#include "game/core/core_streams.h"
#include "game/effects/effects.h"
#include "game/models/figure_manager.h"
#include "game/app/singletons.h"
#include "game/terrain/ground_print.h"
#include "game/items/item.h"
#include "game/effects/lightning.h"
#include "game/models/link_loader.h"
#include "game/models/material.h"
#include "game/net/net_link.h"   // netlink::NetLink: the world handle's pending link
#include "game/world/hoover.h"     // Hoover
#include "game/world/diplomacy.h"
#include "game/data/options.h"
#include "game/players/player_manager.h"
#include "game/core/random.h"
#include "game/data/registry.h"
#include "game/data/resource_manager.h"
#include "game/script/script.h"
#include "game/ai/sector_map.h"
#include "game/scene/sky_object.h"
#include "game/sound/sound.h"
#include "game/spells/spell.h"
#include "game/terrain/terrain.h"
#include "game/data/text_manager.h"
#include "game/ui/ui_flying_hit_points.h"
#include "game/units/unit.h"
#include "game/objects/map_object_manager.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>

void World::Create() {
    paused = 0;
    g_dataFolder = options::g_options.workPath;
    g_textFolder = options::g_options.resourcesPath;
    tick = 0;
    // The wind: along x, no strength.
    windDirection = {1.0f, 0.0f, 0.0f};
    updateCount = 0;
    windStrength = 0.0f;

    players = std::make_unique<PlayerManager>();
    players->Init(this);
    objectManager = std::make_unique<MapObjectManager>();
    objectManager->Init(this);
    figureManager = std::make_unique<FigureManager>();
    figureManager->Init(this);
    timedObjectManager = std::make_unique<EffectManager>();
    timedObjectManager->Init(this);
    spellConstructor = std::make_unique<SpellConstructor>();
    spellConstructor->Create();
    spellManager = std::make_unique<SpellManager>();
    spellManager->Init(this);
    itemConstructor = std::make_unique<ItemConstructor>();
    itemConstructor->Create();
    itemManager = std::make_unique<ItemManager>();
    itemManager->Init(this);

    ShareAssignOwned(worldHandle, new WorldHandle());
    g_random.Init();
    g_clientRandom.InitFrom(g_random);
    g_particleRandom.InitFrom(g_clientRandom);
    countdowns.entries.clear();
}

World::World(ContextKind kind) : ManagerContext(kind) {}

bool World::RunTick() { return true; }

Unit* World::FindUnit(int unitId) {
    return UnitOrNull(ObjectWithId(unitId));
}

MapObject* World::ObjectWithId(int objectId) {
    return GetMapObjectManager()->FindMapObject(static_cast<unsigned>(objectId));
}

// --- World construct ---------------------------------------------------------------------------------

// The two worlds (g_serverWorld, g_clientWorld) and their constructors: WorldServer and WorldClient. The
// members with C++ constructors (the strings, the zone header) are the compiler's; the others' constructors are
// written out in the original's order (the ones written out in the constructors) or called (the out-of-line ones).

namespace {

// The client's flags after construction: the draws.
constexpr WorldClientFlag kClientFlags = WorldClientFlag::DrawObjects | WorldClientFlag::DrawUnits | WorldClientFlag::DrawTerrain |
                                         WorldClientFlag::DrawEffects | WorldClientFlag::DrawSky |
                                         WorldClientFlag::DrawNumbers;
static_assert(lib::Bits(kClientFlags) == 0x35C0);

} // namespace

WorldServer::WorldServer() : World(ContextKind::Server) {
    diplomacy.Reset();
    ResetState();
}

WorldClient::WorldClient() : World(ContextKind::Client) {
    // Both sector grids at 1 x 1 (Create makes them 4 x 4).
    objectGrid.allocatedWidth = 1;
    objectGrid.allocatedHeight = 1;
    objectGrid.Allocate();
    secondGrid.allocatedWidth = 1;
    secondGrid.allocatedHeight = 1;
    secondGrid.Allocate();
    flags |= kClientFlags;
}

// ---- The two worlds -------------------------------------------------------------------------------

// Constructed by the C++ runtime (the client first, as the original's static initialisers did) and destroyed
// after the game's exit functions; GameMain empties them at exit before those run (EmptyWorlds, app/main_loop.cpp), and
// their destructors rely on that.
WorldClient g_clientWorld;
WorldServer g_serverWorld;

// --- World destructors -------------------------------------------------------------------------------

// The worlds' own teardown (Destroy) is not repeated in the destructors: EmptyWorlds (app/main_loop.cpp) has emptied both
// before they run, which also keeps them from touching the camera and the scene renderer, plain globals that may be
// destroyed first. See docs/notes/standalone.md.

World::~World() {
    DestroyOwned(netSession);
    DestroyOwned(worldHandle);
    figureLoader.Reset();
}

ParticleManager::ParticleManager() {
    ParticleManagerClearQuietStart(this);
}

ParticleManager::~ParticleManager() {
    ParticleManager::Destroy();
    freeParticles.clear();
}



WorldClient::~WorldClient() {
    DestroyOwned(handle);
    scorchMarks.clear();
    footprints.clear();
    bloodPrints.clear();
    secondGrid.Free();
    objectGrid.Free();
    dynamicObjects.clear();
    exitMarkers.clear();
    exits.clear();
}

WorldServer::~WorldServer() {
    sharedObjects.clear();
    ReleaseAndClear(counted);
    projectiles.clear();
    diplomacy.Reset();
    exits.clear();
}

// --- World managers ----------------------------------------------------------------------------------

// The world's managers released (the base world's Destroy). Their creation is world_create.cpp. See world.h
// and docs/notes/combat.md.

namespace {

constexpr const char* kPlayerManagerName = "CPlayerManager";
constexpr const char* kEffectManagerName = "CEffectManager";
constexpr const char* kItemManagerName = "CItemManager";

} // namespace

void World::Destroy() {
    if (GetSpellManager()) {
        ObjectListIterator objects;
        for (EngineObject* object = objects.First(GetSpellManager()); object; object = objects.Next()) object->ClearState();
    }
    // In this order: an object's teardown may reach a manager deleted after its own.
    players.reset();
    DestroyOwned(worldHandle);
    DestroyOwned(netSession);
    objectManager.reset();
    figureManager.reset();
    figureLoader.Reset();
    specificManager.reset();
    itemConstructor.reset();
    itemManager.reset();
    spellConstructor.reset();
    spellManager.reset();
    timedObjectManager.reset();
}

// ---- The managers' own small virtuals -------------------------------------
// ClassName / GetClassId and the destructors of the player manager, the effect manager and the item manager. The deleting
// destructor adds the delete.

PlayerManager::PlayerManager() {}
PlayerManager::~PlayerManager() {}
const char* PlayerManager::ClassName() { return kPlayerManagerName; }
ClassId PlayerManager::GetClassId() { return ClassId::PlayerManager; }

EffectManager::EffectManager() {}
EffectManager::~EffectManager() {}
const char* EffectManager::ClassName() { return kEffectManagerName; }
ClassId EffectManager::GetClassId() { return ClassId::EffectManager; }

ItemManager::ItemManager() {}
ItemManager::~ItemManager() {}
const char* ItemManager::ClassName() { return kItemManagerName; }
ClassId ItemManager::GetClassId() { return ClassId::ItemManager; }


// --- World sessions ----------------------------------------------------------------------------------

// The worlds' player sources (player_source.h): the base's destructor, World::worldHandle (WorldHandle) and
// netSession (netlink::DriverSession): their clones, shutdowns and destructors, and World::OpenSession. The
// session's lists: drivers owning (SharedRef: DestroyOwned), holders plain (HeldRef: Release).

using netlink::DriverSession;

PlayerSource::~PlayerSource() {}

// A new helper with the same pending link (counted).
WorldHandle* WorldHandle::Clone() {
    auto* copy = new WorldHandle();
    copy->pendingLink.Assign(pendingLink);
    return copy;
}

// The helper is destroyed and built again in place (the count and flags kept): the link released, none pending.
void WorldHandle::OnShutdown() {
    pendingLink.Reset();
}

WorldHandle::~WorldHandle() { pendingLink.Reset(); }

ClientHandle* ClientHandle::Clone() { return new ClientHandle(); }

void ClientHandle::TakeNew(std::vector<HeldRef<netlink::NetLink>>* links) { links->clear(); }

// ---- netlink::DriverSession -----------------------------------------------------------------------

namespace netlink {

DriverSession::DriverSession() {}

// A copy: every driver shared (a copy-on-share driver is cloned), every holder counted.
DriverSession* DriverSession::Clone() {
    auto* copy = new DriverSession();
    copy->drivers.insert(copy->drivers.begin(), drivers.begin(), drivers.end());
    copy->holders.insert(copy->holders.begin(), holders.begin(), holders.end());
    return copy;
}

// The session is destroyed and built again in place (the count and flags kept): both lists emptied, the
// holders first.
void DriverSession::OnShutdown() {
    holders.clear();
    drivers.clear();
}

// The holders are released before the drivers (members: the reverse order).
DriverSession::~DriverSession() {}

} // namespace netlink

void World::OpenSession() { ShareAssignOwned(netSession, new DriverSession()); }

void World::CloseSession() { DestroyOwned(netSession); }

// --- World singletons --------------------------------------------------------------------------------

// What the game's code reaches through the two worlds and the scene renderer, named once (app/singletons.h): the
// client world's managers, the server world's mission script and sector map, the render device.

Script* TheMissionScript() { return g_serverWorld.MainScript(); }
