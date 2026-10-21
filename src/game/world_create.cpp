// 0055EE40: the world's managers created (the server's and the client's world both call it, and
// it is a virtual slot of their base, vtable entry 0073F194). See docs/notes/combat.md ("Rewritten"),
// the 0055C000-00560000 managers.
//
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
#include "ei/vcall.h"

#include "lib/crt.h"

#include <cstdint>
#include <cstring>
#include "originals_core.h"
#include "originals_world.h"
#include "originals_library.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/mfc_slots.h"

namespace {

// 006D43E0 the world's +0x70 set up.

using Construct = void* (__fastcall*)(void*);

// operator new, the constructor (null without memory), an optional vtable, the reference.
template <typename R = CEngineObject>
R* Make(std::size_t size, Construct construct, const void* vtable) {
    void* memory = gamecrt::operator_new(size);
    R* object = nullptr;
    if (memory) {
        object = static_cast<R*>(construct(memory));
        if (vtable) {
            object = static_cast<R*>(memory);
            ei::SetVtable(object, vtable);
        }
    }
    ++object->refCount_;   // the original counts the reference without testing the pointer
    return object;
}

// A manager attached to the world (its slot 13), after the world holds it.
void Attach(CBaseManager* manager, CWorld* world) { manager->Init(world); }

} // namespace

int __fastcall WorldCreateManagers(CWorld* world) {
    world->paused_ = 0;
    TheDataFolder() = worlddata::ConfiguredDataPath();
    *reinterpret_cast<mfc::CString*>(worlddata::TextFolderString()) = worlddata::ConfiguredTextPath();
    *reinterpret_cast<mfc::CString*>(worlddata::BaseFolderString()) = worlddata::ConfiguredBasePath();
    world->tick_ = 0;
    // The wind: along x, no strength.
    const float direction[3] = {1.0f, 0.0f, 0.0f};
    std::memcpy(world->windDirection_, direction, sizeof(direction));
    world->updateCount_ = 0;
    world->windStrength_ = 0;

    world->players_ = Make<CBaseManager>(sizeof(CPlayerManager), ConstructOn<CPlayerManager, &CPlayerManager::Construct>, nullptr);
    Attach(world->players_, world);
    world->manager40_ = Make<CBaseManager>(0xBC, ConstructOn<CMapObjectManager, &CMapObjectManager::Construct>, nullptr);   // 005125E0
    Attach(world->manager40_, world);
    world->manager3C_ = Make<CBaseManager>(0xD0, ConstructOn<CFigureManager, &CFigureManager::Construct>, nullptr);   // 005AA070
    Attach(world->manager3C_, world);
    world->manager48_ = Make<CBaseManager>(sizeof(CEffectManager), ConstructOn<CEffectManager, &CEffectManager::Construct>, nullptr);
    Attach(world->manager48_, world);
    world->manager50_ = Make(0x24, ConstructOn<CSpellConstructor, &CSpellConstructor::Construct>, nullptr);   // 00683FF0
    world->manager50_->Create();
    world->manager4C_ = Make<CBaseManager>(0xD4, ConstructOn<CSpellManager, &CSpellManager::Construct>, nullptr);   // 00688840
    Attach(world->manager4C_, world);
    world->manager58_ = Make(0x34, ConstructOn<CItemConstructor, &CItemConstructor::Construct>, nullptr);   // 006A5AB0
    world->manager58_->Create();
    world->itemManager_ = Make<CBaseManager>(CItemManager::kSize, ConstructOn<CItemManager, &CItemManager::Construct>, nullptr);
    Attach(world->itemManager_, world);

    // The helper goes into the counted pointer at +0x5C (RefPtr::ShareAssign, inlined).
    auto* helper = static_cast<CWorldHandle*>(gamecrt::operator_new(sizeof(CWorldHandle)));
    // The count and both flag bytes as one dword, written before the test as the original does.
    const std::uint32_t noReferences = 0;
    std::memcpy(&helper->refs_, &noReferences, sizeof(noReferences));
    if (helper) {
        helper->pendingLink_ = nullptr;
        ei::SetVtable(helper, CWorldHandle::kVtableAddress);
    }
    world->shared5C_.ShareAssign(helper);
    IsaacRandom::Global().Init();
    world->countdowns_.RemoveAll();
    return 1;
}
EI_REPLACE(0x0055EE40, WorldCreateManagers);

CWorld* CWorld::ConstructBase() {
    ClearFields();
    figureLoader_ = nullptr;
    shared5C_.ptr = nullptr;
    netSession_ = nullptr;
    countdowns_.Construct(mfc::impl::CountdownArrayClass::Vtable());
    ei::SetVtable(this, kBaseVtableAddress);
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
    return this;
}
EI_REWRITTEN(0x0055EAB0, CWorld::ConstructBase);

CUnit* CWorld::FindUnit(int id) {
    return UnitOrNull(ObjectWithId(id));
}
EI_REPLACE_METHOD(0x0055FB00, CWorld::FindUnit);

CEngineObject* CWorld::FindMapObject(int id) {
    return MapObjectManager()->FindObject(static_cast<unsigned>(id));
}
EI_REPLACE_METHOD(0x0055FAE0, CWorld::FindMapObject);
