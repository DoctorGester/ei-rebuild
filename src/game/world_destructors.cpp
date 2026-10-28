// The destructors the compiler wrote for the world's classes: the bodies the exe's vtables reach through the
// scalar deleting destructors, the members destroyed last to first after the class's own teardown. The
// containers are the library's (lib/mfc.h CArray / CList, std::vector, lib/stlp.h
// hash_map). See docs/notes/standalone.md.
#include "effects.h"
#include "game_arrays.h"
#include "game_lists.h"
#include "ground_print.h"
#include "model.h"
#include "record_lifetime.h"
#include "registry.h"
#include "script.h"
#include "sector_map.h"
#include "spell.h"
#include "terrain.h"
#include "world.h"
#include "world_data.h"
#include "world_deleting_destructors.h"

#include "lib/crt.h"
#include "game_data.h"
#include "lib/mfc_slots.h"
#include "game/record_arrays.h"
#include "game/game_slots.h"

#include <memory>

void RefPtrRelease(RefPtr* self);

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
        for (; count > 0; --count, ++slot) ReleaseCounted(*slot);
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

CWorld::~CWorld() {
    CWorld::Destroy();
    countdowns_.vtable = mfc::impl::CountdownArrayClass::Vtable();
    countdowns_.FreeData();
    netSession_.Destroy();
    shared5C_.Destroy();
    if (CLinkFigureLoader* loader = figureLoader_)
        if (--loader->refCount_ == 0) delete loader;
}

CParticleManager::CParticleManager() {
    mfc::impl::ConstructListHeader<mfc::impl::ClientListClass>(&freeParticles, 10);
    maxHeldId_ = 0;
    ParticleManagerClearQuietStart(this);
}

CParticleManager::~CParticleManager() {
    CParticleManager::Destroy();
    freeParticles.vtable = mfc::impl::ClientListClass::Vtable();
    freeParticles.RemoveAll();
}

Sector::~Sector() {
    Clear();
    occluders.vtable = mfc::impl::SectorOccluderArrayClass::Vtable();
    occluders.FreeData();
    units.vtable = mfc::impl::PtrListClass::Vtable();
    units.RemoveAll();
    objects.vtable = mfc::impl::ClientObjectListClass::Vtable();
    objects.RemoveAll();
}

CWorldClient::~CWorldClient() {
    CWorldClient::Destroy();
    handle_.Destroy();
    scorchMarks_.Release<PrintElement>();
    footprints_.Release<PrintElement>();
    bloodPrints_.Release<PrintElement>();
    secondGrid_.Resize(1, 1);
    FreeGrid(secondGrid_);
    objectGrid_.Resize(1, 1);
    FreeGrid(objectGrid_);
    dynamicObjects_.vtable = mfc::impl::ClientObjectListClass::Vtable();
    dynamicObjects_.RemoveAll();
    objects_.Destroy();
    DestroySectorList(&heroes_);
    countedA0_.vtable = gameslots::ClientObjectPtrArrayClass::Vtable();
    countedA0_.ReleaseAll();
    gamearrays::Exits::Destruct<mfc::impl::ExitArrayClass>(&exits_);
}

CWorldServer::~CWorldServer() {
    CWorldServer::Destroy();
    DestroyList<SharedObjectElements>(sharedObjects_, gameslots::SharedObjectListClass::Vtable());   // CList<SharedObject>
    DestroyList<ReleasedElements>(counted_, gameslots::ItemObjectListClass::Vtable());   // CList<CEngineObject*>
    DestroyList<ProjectileElements>(projectiles_, gameslots::RefListBClass::Vtable());   // CList<CSpellEffect*>
    diplomacy_.Reset();
    SecondScript()->Destruct();
    MainScript()->Destruct();
    Messages()->Destroy();
    gamearrays::Exits::Destruct<mfc::impl::ExitArrayClass>(&exits_);
}
