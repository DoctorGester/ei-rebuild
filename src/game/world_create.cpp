// The world's managers created (the server's and the client's world both call it, and it is a virtual
// of their base). See docs/notes/combat.md ("Rewritten"), the managers.
// The world's paths are taken from the command line globals; every manager is made, referenced and
// attached to the world (ThisWorld with the world, or the base); a 0x0C-byte helper goes into a
// counted pointer (shared5C_); then the ISAAC generator is seeded and countdowns_ set up.
#include "world.h"
#include "model.h"   // TweenArray (countdowns_)
#include "world_data.h"

#include "base_manager.h"
#include "figure_manager.h"
#include "item.h"
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
    ++object->refCount_;
    return object;
}

// A manager attached to the world, after the world holds it.
void Attach(CBaseManager* manager, CWorld* world) { manager->Init(world); }

} // namespace

int CWorld::Create() {
    paused_ = 0;
    *g_dataFolder = worlddata::ConfiguredDataPath();
    *g_textFolder = worlddata::ConfiguredTextPath();
    *g_discFolder = worlddata::ConfiguredBasePath();
    tick_ = 0;
    // The wind: along x, no strength.
    const float direction[3] = {1.0f, 0.0f, 0.0f};
    std::memcpy(windDirection_, direction, sizeof(direction));
    updateCount_ = 0;
    windStrength_ = 0;

    players_ = MakeNew<CPlayerManager>();
    Attach(players_, this);
    manager40_ = MakeNew<CMapObjectManager>();
    Attach(manager40_, this);
    manager3C_ = MakeNew<CFigureManager>();
    Attach(manager3C_, this);
    manager48_ = MakeNew<CEffectManager>();
    Attach(manager48_, this);
    manager50_ = MakeNew<CSpellConstructor>();
    manager50_->Create();
    manager4C_ = MakeNew<CSpellManager>();
    Attach(manager4C_, this);
    manager58_ = MakeNew<CItemConstructor>();
    manager58_->Create();
    itemManager_ = MakeNew<CItemManager>();
    Attach(itemManager_, this);

    shared5C_.ShareAssign(new CWorldHandle());
    g_random.Init();
    countdowns_.clear();
    return 1;
}

CWorld::CWorld() {
    ClearFields();
    figureLoader_ = nullptr;
    shared5C_.ptr = nullptr;
    netSession_.ptr = nullptr;
    players_ = nullptr;
    manager3C_ = nullptr;
    manager40_ = nullptr;
    manager44_ = nullptr;
    manager48_ = nullptr;
    manager4C_ = nullptr;
    manager50_ = nullptr;
    itemManager_ = nullptr;
    manager58_ = nullptr;
    timeBase_ = 0.0f;
    secondsPerTick_ = 0.0f;
}

int CWorld::RunTick() { return 1; }

CUnit* CWorld::FindUnit(int id) {
    return UnitOrNull(ObjectWithId(id));
}

CMapObject* CWorld::ObjectWithId(int id) {
    return MapObjectManager()->FindMapObject(static_cast<unsigned>(id));
}
