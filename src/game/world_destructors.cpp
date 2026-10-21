// The destructors the compiler wrote for the world's classes (S13a): the bodies the exe's vtables
// reach through their slot 0 (the scalar deleting destructors), the members destroyed last to
// first after the class's own teardown. The containers are the library's (lib/mfc.h CArray /
// CList, lib/containers.h AlignedArray, lib/stlp.h hash_map). Checked in game by
// verify_world_destructors.cpp until S21d. docs/notes/standalone.md, S13a.
#include "effects.h"
#include "game_arrays.h"
#include "game_lists.h"
#include "ground_print.h"
#include "model.h"
#include "originals_world.h"
#include "record_lifetime.h"
#include "registry.h"
#include "script.h"
#include "sector_map.h"
#include "terrain.h"
#include "world.h"
#include "world_data.h"
#include "world_deleting_destructors.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "game_data.h"
#include "lib/mfc_slots.h"
#include "game/record_arrays.h"
#include "game/game_slots.h"

void __fastcall RefPtrRelease(RefPtr* self);   // refptr_copies.cpp

namespace {

// The members' vtables the destructors store back before emptying them.
constexpr std::uintptr_t kParticleListVtable = std::uintptr_t(0x0073D018);   // CTypedPtrList<CPtrList, Particle*>
constexpr std::uintptr_t kWaterLevelsVtable = std::uintptr_t(0x0073F730);    // CArray<CArray<float>>
constexpr std::uintptr_t kSectorCacheVtable = std::uintptr_t(0x0073F724);    // CList<SectorData*>
constexpr std::uintptr_t kClientVtable = std::uintptr_t(0x0073F41C);
constexpr std::uintptr_t kServerVtable = std::uintptr_t(0x0073F928);

// The client's ground print lists: each print's destructor (the client's copies 0056D080,
// 0056D400, 0056D680 of GroundPrint's).
struct PrintElement {
    template <typename P>
    static void Destroy(P* print) {
        print->Destruct();
    }
};

// A sector grid's storage as the client's destructor frees it after setting the grid to 1 x 1: the
// cells' lists (their vector deleting destructor), then the row pointers.
void FreeGrid(SectorGrid& grid) {
    if (grid.cells_) SectorListsDelete(grid.cells_, nullptr, 3);
    gamecrt::operator_delete(grid.rows_);
}

// The server's lists' element destructors (their RemoveAll rows 0059A370, 00429E50, 00599C70).
// A shared object: its array of counted objects (the body 00594950: the base vtable, every object
// released through slot 4, the data freed).
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
// A counted object: released through slot 4.
struct ReleasedElements {
    static void Construct(CEngineObject**, int) {}
    static void Destruct(CEngineObject** slot, int count) {
        for (; count > 0; --count, ++slot)
            if (*slot) (*slot)->Release();
    }
};
// A missile: its counted pointer released (0059B4B0).
struct ProjectileElements {
    static void Construct(Projectile**, int) {}
    static void Destruct(Projectile** slot, int count) {
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

// 0058A140 (the server's second script inlines it, 0058AC5E..0058AD44): the reset, the error text,
// then the containers last to first (the variables through ScriptVarsDestruct).
void CScript::Destruct() {
    ScriptReset(this);
    errorText_.~CString();
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

void CWorld::Destruct() {
    ei::SetVtable(this, kBaseVtableAddress);
    DestroyManagers();
    auto* countdowns = &countdowns_;
    countdowns->vtable = mfc::impl::CountdownArrayClass::Vtable();
    countdowns->FreeData();
    reinterpret_cast<RefPtr*>(&netSession_)->Destroy();
    shared5C_.Destroy();
    if (CLinkFigureLoader* loader = figureLoader_)
        if (--loader->refCount_ == 0) loader->DeleteThis(1);
    CEngineObject::DestructBase();
}
EI_REWRITTEN(0x0055EC80, CWorld::Destruct);

CWorld* CWorld::DeletingDestruct(unsigned flags) {
    Destruct();
    return DeleteIf(this, flags);
}
EI_REPLACE_METHOD(0x0055EC60, CWorld::DeletingDestruct);

CParticleManager::CParticleManager() {
    mfc::impl::ConstructListHeader<mfc::impl::ClientListClass>(&freeParticles, 10);
    held_.Construct(100);   // 005725E0
    maxHeldId_ = 0;
    ParticleManagerClearQuietStart(this);   // 004981A0
}

CParticleManager::~CParticleManager() {
    CParticleManager::Destroy();
    held_.Destroy();
    freeParticles.vtable = ei::GamePointer<void>(kParticleListVtable);
    freeParticles.RemoveAll();
}

void CParticleManager::Destruct() { this->CParticleManager::~CParticleManager(); }
EI_REWRITTEN(0x005627C0, CParticleManager::Destruct);

CParticleManager* CParticleManager::DeletingDestruct(unsigned flags) {
    this->CParticleManager::~CParticleManager();
    return DeleteIf(this, flags);
}
EI_REWRITTEN(0x005627A0, CParticleManager::DeletingDestruct);

void Terrain::Destruct() {
    ei::SetVtable(this, worlddata::kTerrainVtable);
    Terrain::DestroyImpl();
    waterLevels.vtable = ei::GamePointer<void>(kWaterLevelsVtable);
    waterLevels.FreeData();
    textAE0.~CString();
    visibleColumnLast.FreeData();
    visibleColumnFirst.FreeData();
    wavePhases.FreeData();
    lights.Destroy();
    sectorCache.vtable = ei::GamePointer<void>(kSectorCacheVtable);
    sectorCache.RemoveAll();
    vertexTiles.FreeData();
    archive.Destroy();
    textureFrames.FreeData();
    textureClass.FreeData();
    materials.FreeData();
    textFC.~CString();
    textF8.~CString();
    pageMaterials.FreeData();
    pageTextures.FreeData();
    ei::SetVtable(this, CTransformObject::kVtableAddress);
    CEngineObject::DestructBase();
}
EI_REWRITTEN(0x00562B80, Terrain::Destruct);

Terrain* Terrain::DeletingDestruct(unsigned flags) {
    Destruct();
    return DeleteIf(this, flags);
}
EI_REPLACE_METHOD(0x00562B60, Terrain::DeletingDestruct);

void Sector::Destruct() {
    ei::SetVtable(this, kSectorVtable);
    Clear();
    occluders.vtable = ei::GamePointer<void>(kSectorOccluderArrayVtable);
    occluders.FreeData();
    units.vtable = mfc::PtrListVtable();
    units.RemoveAll();
    objects.vtable = mfc::impl::ClientObjectListClass::Vtable();
    objects.RemoveAll();
}
EI_REWRITTEN(0x005BFCD0, Sector::Destruct);

Sector* Sector::DeletingDestruct(unsigned flags) {
    Destruct();
    return DeleteIf(this, flags);
}
EI_REPLACE_METHOD(0x0059E080, Sector::DeletingDestruct);

void CWorldClient::Destruct() {
    ei::SetVtable(this, kClientVtable);
    Teardown();
    pausedTitle_.~CString();
    windowTitle_.~CString();
    windowClass_.~CString();
    appName_.~CString();
    list7BC_.Destroy();
    RainSnow()->DestructBody();
    texturePath_.~CString();
    platforms_.DestructBody();
    handle_.Destroy();
    scorchMarks_.Release<PrintElement>();
    footprints_.Release<PrintElement>();
    bloodPrints_.Release<PrintElement>();
    zoneName_.~CString();
    secondGrid_.Resize(1, 1);
    FreeGrid(secondGrid_);
    objectGrid_.Resize(1, 1);
    FreeGrid(objectGrid_);
    lineLists_.Destroy();
    reinterpret_cast<stlp::vector<ScreenEntry>*>(&hits_)->FreeStorage();
    screenEntries_.Destroy();
    dynamicNodes_.Destroy();
    dynamicObjects_.vtable = mfc::impl::ClientObjectListClass::Vtable();
    dynamicObjects_.RemoveAll();
    objects_.Destroy();
    DestroySectorList(&heroes_);
    countedA0_.vtable = gameslots::ClientObjectPtrArrayClass::Vtable();   // the client's +0xA0
    if (countedA0_.data) {
        for (int i = 0; i < countedA0_.size; ++i) ObjectHolderDelete(&countedA0_.data[i], nullptr, 0);
        gamecrt::operator_delete(countedA0_.data);
    }
    gamearrays::Exits::Destruct<mfc::impl::ExitArrayClass>(reinterpret_cast<gamearrays::Exits::Array*>(&exitsVtable_));
    CWorld::Destruct();
}
EI_REPLACE_METHOD(0x00561B00, CWorldClient::Destruct);

CWorldClient* CWorldClient::DeletingDestruct(unsigned flags) {
    Destruct();
    return DeleteIf(this, flags);
}
EI_REPLACE_METHOD(0x00561A00, CWorldClient::DeletingDestruct);

void CWorldServer::Destruct() {
    ei::SetVtable(this, kServerVtable);
    Teardown();
    DestroyList<SharedObjectElements>(sharedObjects_, gameslots::SharedObjectListClass::Vtable());   // CList<SharedObject>
    DestroyList<ReleasedElements>(counted_, gameslots::ItemObjectListClass::Vtable());   // CList<CEngineObject*>
    DestroyList<ProjectileElements>(projectiles_, gameslots::RefListBClass::Vtable());   // CList<Projectile*>
    zoneHeader_.Destruct();
    timedObjects_.map.Destroy();
    namedObjects_.map.Destroy();
    otherMapObjects_.map.Destroy();
    mapObjects_.map.Destroy();
    sentObjects_.map.Destroy();
    objects_.map.Destroy();
    mapName_.~CString();
    mobName_.~CString();
    diplomacy_.Reset();
    SecondScript()->Destruct();
    MainScript()->Destruct();
    DestructAIMap(AIMap());
    Messages()->Destroy();
    mobFiles_.Destruct();
    zoneName_.~CString();
    updateMap_.map.Destroy();
    gamearrays::Exits::Destruct<mfc::impl::ExitArrayClass>(reinterpret_cast<gamearrays::Exits::Array*>(&exitsVtable_));
    CWorld::Destruct();
}
EI_REPLACE_METHOD(0x0058AAD0, CWorldServer::Destruct);

CWorldServer* CWorldServer::DeletingDestruct(unsigned flags) {
    Destruct();
    return DeleteIf(this, flags);
}
EI_REPLACE_METHOD(0x0058A430, CWorldServer::DeletingDestruct);
