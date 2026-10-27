// The destructors the compiler wrote for the world's classes: the bodies the exe's vtables reach through the
// scalar deleting destructors, the members destroyed last to first after the class's own teardown. The
// containers are the library's (lib/mfc.h CArray / CList, lib/containers.h AlignedArray, lib/stlp.h
// hash_map). Checked in game by verify_world_destructors.cpp. See docs/notes/standalone.md.
#include "effects.h"
#include "game_arrays.h"
#include "game_lists.h"
#include "ground_print.h"
#include "model.h"
#include "record_lifetime.h"
#include "registry.h"
#include "script.h"
#include "sector_map.h"
#include "terrain.h"
#include "world.h"
#include "world_data.h"
#include "world_deleting_destructors.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "game_data.h"
#include "lib/mfc_slots.h"
#include "game/record_arrays.h"
#include "game/game_slots.h"

#include <memory>

void __fastcall RefPtrRelease(RefPtr* self);

namespace {

// The client's ground print lists: each print's destructor (the client's copies of GroundPrint's).
struct PrintElement {
    template <typename P>
    static void Destroy(P* print) {
        print->Destruct();
    }
};

// A sector grid's storage as the client's destructor frees it after setting the grid to 1 x 1: the
// cells' lists (their vector deleting destructor), then the row pointers.
void FreeGrid(SectorGrid& grid) {
    if (grid.cells_) grid.cells_->VectorDelete(3);
    gamecrt::operator_delete(grid.rows_);
}

// The server's lists' element destructors (their RemoveAll rows).
// A shared object: its array of counted objects (the base vtable, every object released through
// Release, the data freed).
struct SharedObjectElements {
    static void Construct(SharedObject*, int) {}
    static void Destruct(SharedObject* entry, int count) {
        for (; count > 0; --count, ++entry) {
            auto& objects = entry->objects;
            objects.vtable = gamearrays::ObjectPtrArrayBaseClass::Vtable();
            if (!objects.data) continue;
            for (int i = 0; i < objects.size; ++i)
                if (CEngineObject* object = objects.data[i]) object->Release();
            gamecrt::operator_delete(objects.data);
        }
    }
};
// A counted object: released through Release.
struct ReleasedElements {
    static void Construct(CEngineObject**, int) {}
    static void Destruct(CEngineObject** slot, int count) {
        for (; count > 0; --count, ++slot)
            if (*slot) (*slot)->Release();
    }
};
// A missile: its counted pointer released.
struct ProjectileElements {
    static void Construct(CSpellEffect**, int) {}
    static void Destruct(CSpellEffect** slot, int count) {
        for (; count > 0; --count, ++slot) RefPtrRelease(reinterpret_cast<RefPtr*>(slot));
    }
};
// A list emptied as its instantiation's destructor does: the vtable back, then RemoveAll with the
// elements destroyed.
template <typename Elements, typename T>
void DestroyList(mfc::CList<T>& list, void* vtable) {
    auto& typed = reinterpret_cast<mfc::CList<T, Elements>&>(list);
    typed.vtable = vtable;
    typed.RemoveAll();
}

// A script container destroyed by its instantiation's destructor (`Abi` over the element's policy,
// `Class` the vtable it writes back).
template <typename Abi, typename Class, typename Container>
void DestroyScriptList(Container& list) {
    Abi::template Destruct<Class>(reinterpret_cast<typename Abi::List*>(&list));
}
template <typename Abi, typename Class, typename Container>
void DestroyScriptArray(Container& array) {
    Abi::template Destruct<Class>(reinterpret_cast<typename Abi::Array*>(&array));
}

template <typename T>
T* DeleteIf(T* self, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

} // namespace

// (The server's second script inlines it): the reset, the error text,
// then the containers last to first (the variables through ScriptVarsDestruct).
void CScript::Destruct() {
    ScriptReset(this);
    std::destroy_at(&errorText_);
    DestroyScriptList<gamelists::ObjectRefs, gameslots::ObjectRefListClass>(tempObjects_);
    DestroyScriptList<gamelists::Groups, gameslots::GroupListClass>(tempGroups_);
    DestroyScriptArray<gamearrays::Areas, mfc::impl::ScriptAreaArrayClass>(areas_);
    DestroyScriptList<gamelists::Decls, gameslots::DeclListClass>(worldBlocks_);
    DestroyScriptList<gamelists::Runs, gameslots::RunListClass>(runs_);
    DestroyScriptList<gamelists::Definitions, gameslots::DefinitionListClass>(instances_);
    ScriptVarsDestruct(&globals_);
    DestroyScriptArray<gamearrays::Definitions, recordarrays::DefinitionArrayClass>(definitions_);
    DestroyScriptList<gamelists::Decls, gameslots::DeclListClass>(blocks_);
    DestroyScriptList<gamelists::Lightnings, gameslots::LightningListClass>(lightning_);
    DestroyScriptList<gamelists::FxSources, gameslots::FxSourceListClass>(fxSources_);
    DestroyScriptList<gamelists::ParticleSources, gameslots::ParticleSourceListClass>(particles_);
    DestroyScriptArray<gamearrays::PointLights, gamearrays::PointLightArrayClass>(pointLights_);
}
EI_REWRITTEN(0x0058A140, CScript::Destruct);

CWorld::~CWorld() {
    CWorld::Destroy();
    countdowns_.vtable = mfc::impl::CountdownArrayClass::Vtable();
    countdowns_.FreeData();
    netSession_.Destroy();
    shared5C_.Destroy();
    if (CLinkFigureLoader* loader = figureLoader_)
        if (--loader->refCount_ == 0) delete loader;
}
EI_REWRITTEN_SPECIAL(0x0055EC80, CWorld::~CWorld);
EI_REWRITTEN_SPECIAL(0x0055EC60, CWorld::~CWorld);

CParticleManager::CParticleManager() {
    mfc::impl::ConstructListHeader<mfc::impl::ClientListClass>(&freeParticles, 10);
    held_.Construct(100);
    maxHeldId_ = 0;
    ParticleManagerClearQuietStart(this);
}

CParticleManager::~CParticleManager() {
    CParticleManager::Destroy();
    held_.Destroy();
    freeParticles.vtable = mfc::impl::ClientListClass::Vtable();
    freeParticles.RemoveAll();
}

EI_REWRITTEN_SPECIAL(0x005627C0, CParticleManager::~CParticleManager);

EI_REWRITTEN_SPECIAL(0x005627A0, CParticleManager::~CParticleManager);

Sector::~Sector() {
    Clear();
    occluders.vtable = mfc::impl::SectorOccluderArrayClass::Vtable();
    occluders.FreeData();
    units.vtable = mfc::impl::PtrListClass::Vtable();
    units.RemoveAll();
    objects.vtable = mfc::impl::ClientObjectListClass::Vtable();
    objects.RemoveAll();
}
EI_REWRITTEN_SPECIAL(0x005BFCD0, Sector::~Sector);
EI_REWRITTEN_SPECIAL(0x0059E080, Sector::~Sector);

CWorldClient::~CWorldClient() {
    CWorldClient::Destroy();
    list7BC_.Destroy();
    RainSnow()->~CRainSnow();
    handle_.Destroy();
    scorchMarks_.Release<PrintElement>();
    footprints_.Release<PrintElement>();
    bloodPrints_.Release<PrintElement>();
    secondGrid_.Resize(1, 1);
    FreeGrid(secondGrid_);
    objectGrid_.Resize(1, 1);
    FreeGrid(objectGrid_);
    lineLists_.Destroy();
    hits_.FreeStorage();
    screenEntries_.Destroy();
    dynamicNodes_.Destroy();
    dynamicObjects_.vtable = mfc::impl::ClientObjectListClass::Vtable();
    dynamicObjects_.RemoveAll();
    objects_.Destroy();
    DestroySectorList(&heroes_);
    countedA0_.vtable = gameslots::ClientObjectPtrArrayClass::Vtable();
    if (countedA0_.data) {
        for (int i = 0; i < countedA0_.size; ++i) ReleaseHeld(&countedA0_.data[i]);
        gamecrt::operator_delete(countedA0_.data);
    }
    gamearrays::Exits::Destruct<mfc::impl::ExitArrayClass>(&exits_);
}
EI_REWRITTEN_SPECIAL(0x00561B00, CWorldClient::~CWorldClient);
EI_REWRITTEN_SPECIAL(0x00561A00, CWorldClient::~CWorldClient);

CWorldServer::~CWorldServer() {
    CWorldServer::Destroy();
    DestroyList<SharedObjectElements>(sharedObjects_, gameslots::SharedObjectListClass::Vtable());   // CList<SharedObject>
    DestroyList<ReleasedElements>(counted_, gameslots::ItemObjectListClass::Vtable());   // CList<CEngineObject*>
    DestroyList<ProjectileElements>(projectiles_, gameslots::RefListBClass::Vtable());   // CList<CSpellEffect*>
    timedObjects_.map.Destroy();
    namedObjects_.map.Destroy();
    otherMapObjects_.map.Destroy();
    mapObjects_.map.Destroy();
    sentObjects_.map.Destroy();
    objects_.map.Destroy();
    diplomacy_.Reset();
    SecondScript()->Destruct();
    MainScript()->Destruct();
    Messages()->Destroy();
    mobFiles_.Destruct();
    updateMap_.map.Destroy();
    gamearrays::Exits::Destruct<mfc::impl::ExitArrayClass>(&exits_);
}
EI_REWRITTEN_SPECIAL(0x0058AAD0, CWorldServer::~CWorldServer);
EI_REWRITTEN_SPECIAL(0x0058A430, CWorldServer::~CWorldServer);
