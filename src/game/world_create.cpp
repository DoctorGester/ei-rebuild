// The world's managers created (the server's and the client's world both call it, and it is a virtual
// of their base). See docs/notes/combat.md ("Rewritten"), the managers.
// The world's paths are taken from the command line globals; every manager is made, referenced and
// attached to the world (ThisWorld with the world, or the base); a 0x0C-byte helper goes into a
// counted pointer (worldHandle); then the ISAAC generator is seeded and countdowns set up.
#include "item_manager.h"
#include "world.h"
#include "model.h"   // TweenArray (countdowns)
#include "world_data.h"

#include "base_manager.h"
#include "figure_manager.h"
#include "item.h"
#include "net_link.h"   // netlink::NetLink: the world handle's pending link
#include "player_manager.h"
#include "resource_manager.h"
#include "spell.h"
#include "unit.h"
#include "unit_create.h"
#include "random.h"

#include <cstdint>
#include <cstring>
#include "game_singletons.h"

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
