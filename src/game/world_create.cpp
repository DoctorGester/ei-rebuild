// The world's managers created (the server's and the client's world both call it, and it is a virtual
// of their base). See docs/notes/combat.md ("Rewritten"), the managers.
// The world's paths are taken from the command line globals; every manager is made, referenced and
// attached to the world (ThisWorld with the world, or the base); a 0x0C-byte helper goes into a
// counted pointer (worldHandle); then the ISAAC generator is seeded and countdowns set up.
#include "item_manager.h"

#include "ai_map.h"
#include "base_manager.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "effects.h"
#include "figure_manager.h"
#include "game_singletons.h"
#include "ground_print.h"
#include "item.h"
#include "model.h"   // TweenArray (countdowns)
#include "net_link.h"   // netlink::NetLink: the world handle's pending link
#include "object_diplomacy.h"
#include "player_manager.h"
#include "random.h"
#include "registry.h"
#include "resource_manager.h"
#include "script.h"
#include "sector_map.h"
#include "spell.h"
#include "terrain.h"
#include "texture.h"       // Texture: the fog texture (ClientArrayManager)
#include "unit.h"
#include "unit_create.h"
#include "world.h"
#include "world_data.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <new>

namespace {

// A new object (its class's operator new and constructor), counted once: the reference is counted
// without testing the pointer, like the original.
template <class T>
T* MakeNew() {
    T* object = new T();
    object->AddRef();
    return object;
}

// A manager attached to the world, after the world holds it.
void Attach(BaseManager* manager, World* world) { manager->Init(world); }

} // namespace

void World::Create() {
    paused = 0;
    g_dataFolder = worlddata::ConfiguredDataPath();
    g_textFolder = worlddata::ConfiguredTextPath();
    g_discFolder = worlddata::ConfiguredBasePath();
    tick = 0;
    // The wind: along x, no strength.
    const float direction[3] = {1.0f, 0.0f, 0.0f};
    std::memcpy(windDirection, direction, sizeof(direction));
    updateCount = 0;
    windStrength = 0;

    players = MakeNew<PlayerManager>();
    Attach(players, this);
    objectManager = MakeNew<MapObjectManager>();
    Attach(objectManager, this);
    figureManager = MakeNew<FigureManager>();
    Attach(figureManager, this);
    timedObjectManager = MakeNew<EffectManager>();
    Attach(timedObjectManager, this);
    spellConstructor = MakeNew<SpellConstructor>();
    spellConstructor->Create();
    spellManager = MakeNew<SpellManager>();
    Attach(spellManager, this);
    itemConstructor = MakeNew<ItemConstructor>();
    itemConstructor->Create();
    itemManager = MakeNew<ItemManager>();
    Attach(itemManager, this);

    ShareAssignOwned(worldHandle, new WorldHandle());
    g_random.Init();
    g_clientRandom.InitFrom(g_random);
    countdowns.entries.clear();
}

World::World() {
    players = nullptr;
    figureManager = nullptr;
    objectManager = nullptr;
    specificManager = nullptr;
    timedObjectManager = nullptr;
    spellManager = nullptr;
    spellConstructor = nullptr;
    itemManager = nullptr;
    itemConstructor = nullptr;
    timeBase = 0.0f;
    secondsPerTick = 0.0f;
}

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

// --- World destructors -------------------------------------------------------------------------------

// The destructors the compiler wrote for the world's classes: the bodies the exe's vtables reach through the
// scalar deleting destructors, the members destroyed last to first. The worlds' own teardown (Destroy) is not
// repeated: EmptyWorlds (app_main.cpp) has emptied both before their destructors run, which also keeps them from
// touching the camera and the scene renderer, plain globals that may be destroyed first. The containers are std's.
// See docs/notes/standalone.md.

World::~World() {
    DestroyOwned(netSession);
    DestroyOwned(worldHandle);
    figureLoader.Reset();
}

ParticleManager::ParticleManager() {
    maxHeldId = 0;
    ParticleManagerClearQuietStart(this);
}

ParticleManager::~ParticleManager() {
    ParticleManager::Destroy();
    freeParticles.clear();
}

Sector::~Sector() {
    Clear();
    units.clear();
    objects.clear();
}

WorldClient::~WorldClient() {
    DestroyOwned(handle);
    scorchMarks.clear();
    footprints.clear();
    bloodPrints.clear();
    // The second grid still holds its objects: released here, as the original's destructor does.
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
// The world's reference is dropped and the manager deleted without testing the count.
template <typename T>
void DeleteManager(T*& manager) {
    if (!manager) return;
    --manager->refCount;
    delete manager;
    manager = nullptr;
}

constexpr const char* kPlayerManagerName = "CPlayerManager";
constexpr const char* kEffectManagerName = "CEffectManager";
constexpr const char* kItemManagerName = "CItemManager";
constexpr const char* kItemsFolderName = "items";

} // namespace

void World::Destroy() {
    if (GetSpellManager()) {
        ObjectListIterator objects{nullptr, {}};
        for (EngineObject* object = objects.First(GetSpellManager()); object; object = objects.Next()) object->ClearState();
        if (objects.manager) --objects.manager->refCount;
    }
    DeleteManager(players);
    DestroyOwned(worldHandle);
    DestroyOwned(netSession);
    DeleteManager(objectManager);
    DeleteManager(figureManager);
    figureLoader.Reset();
    DeleteManager(specificManager);
    DeleteManager(itemConstructor);
    DeleteManager(itemManager);
    DeleteManager(spellConstructor);
    DeleteManager(spellManager);
    DeleteManager(timedObjectManager);
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
const char* ItemManager::GetFolderName() { return kItemsFolderName; }

BaseManager* World::GetFigureManager() { return figureManager; }

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

// What the game's code reaches through the two worlds and the scene renderer, named once (game_singletons.h): the
// client world's managers, the server world's mission script and sector map, the render device.

Script* TheMissionScript() { return g_serverWorld.MainScript(); }
