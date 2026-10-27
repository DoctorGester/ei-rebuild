// The world's managers created (the server's and the client's world both call it, and
// it is a virtual slot of their base). See docs/notes/combat.md ("Rewritten"),
// the managers.
// The world's paths are taken from the command line globals; every manager is made, referenced
// and attached to the world (its slot 13 with the world, or slot 2); a 0x0C-byte helper goes into
// a counted pointer at +0x5C; then the ISAAC generator is seeded and +0x70 set up.
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

#include "ei/hook.h"

#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include "game_data.h"
#include "game_singletons.h"
#include "lib/mfc_slots.h"

namespace {

// The world's +0x70 is set up.

// A new object (its class's operator new and constructor), counted once: the original counts the reference
// without testing the pointer.
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
    TheDataFolder() = worlddata::ConfiguredDataPath();
    TheTextFolder() = worlddata::ConfiguredTextPath();
    TheDiscFolder() = worlddata::ConfiguredBasePath();
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

    // The helper goes into the counted pointer at +0x5C (RefPtr::ShareAssign, inlined).
    shared5C_.ShareAssign(new CWorldHandle());
    IsaacRandom::Global().Init();
    countdowns_.RemoveAll();
    return 1;
}
EI_REWRITTEN(0x0055EE40, CWorld::Create);

CWorld::CWorld() {
    ClearFields();
    figureLoader_ = nullptr;
    shared5C_.ptr = nullptr;
    netSession_.ptr = nullptr;
    countdowns_.Construct(mfc::impl::CountdownArrayClass::Vtable());
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
EI_REWRITTEN_SPECIAL(0x0055EAB0, CWorld::CWorld);

int CWorld::RunTick() { return 1; }
EI_REWRITTEN(0x0055FAD0, CWorld::RunTick);

CUnit* CWorld::FindUnit(int id) {
    return UnitOrNull(ObjectWithId(id));
}
EI_REWRITTEN(0x0055FB00, CWorld::FindUnit);

CMapObject* CWorld::ObjectWithId(int id) {
    return ObjectSlots(MapObjectManager()->FindObject(static_cast<unsigned>(id)));   // the map object manager's objects
}
EI_REWRITTEN(0x0055FAE0, CWorld::ObjectWithId);
