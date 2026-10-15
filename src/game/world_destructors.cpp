// The destructors the compiler wrote for the world's classes (S13a): the bodies the exe's vtables
// reach through their slot 0 (the scalar deleting destructors), the members destroyed last to
// first after the class's own teardown. The containers are the library's (lib/mfc.h CArray /
// CList, lib/containers.h AlignedArray, lib/stlp.h hash_map). Checked by
// tests/verify/verify_world_destructors.cpp. docs/notes/standalone.md, S13a.
#include "effects.h"
#include "game_arrays.h"
#include "ground_print.h"
#include "model.h"
#include "originals_world.h"
#include "record_lifetime.h"
#include "sector_map.h"
#include "terrain.h"
#include "world.h"
#include "world_data.h"
#include "world_deleting_destructors.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

namespace {

// The members' vtables the destructors store back before emptying them.
constexpr std::uintptr_t kParticleListVtable = std::uintptr_t(0x0073D018);   // CTypedPtrList<CPtrList, Particle*>
constexpr std::uintptr_t kWaterLevelsVtable = std::uintptr_t(0x0073F730);    // CArray<CArray<float>>
constexpr std::uintptr_t kSectorCacheVtable = std::uintptr_t(0x0073F724);    // CList<SectorData*>
constexpr std::uintptr_t kClientVtable = std::uintptr_t(0x0073F41C);
constexpr std::uintptr_t kCountedArrayVtable = std::uintptr_t(0x0073F53C);   // the client's +0xA0

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

template <typename T>
T* DeleteIf(T* self, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

} // namespace

void CWorld::Destruct() {
    ei::SetVtable(this, kBaseVtableAddress);
    DestroyManagers();
    auto* countdowns = reinterpret_cast<mfc::CArray<int>*>(countdowns_);
    countdowns->vtable = reinterpret_cast<void*>(kCountdownsVtable);
    countdowns->FreeData();
    reinterpret_cast<RefPtr*>(&netSession_)->Destroy();
    shared5C_.Destroy();
    if (CLinkFigureLoader* loader = figureLoader_)
        if (--loader->refCount_ == 0) loader->DeleteThis(1);
    CEngineObject::DestructBase();
}
EI_REPLACE_METHOD(0x0055EC80, CWorld::Destruct);

CWorld* CWorld::DeletingDestruct(unsigned flags) {
    Destruct();
    return DeleteIf(this, flags);
}
EI_REPLACE_METHOD(0x0055EC60, CWorld::DeletingDestruct);

void CParticleManager::Destruct() {
    ei::SetVtable(this, worlddata::kParticleManagerVtable);
    CParticleManager::Destroy();
    held_.Destroy();
    freeParticles.vtable = reinterpret_cast<void*>(kParticleListVtable);
    freeParticles.RemoveAll();
    CBaseManager::DestructBody();
}
EI_REPLACE_METHOD(0x005627C0, CParticleManager::Destruct);

CParticleManager* CParticleManager::DeletingDestruct(unsigned flags) {
    Destruct();
    return DeleteIf(this, flags);
}
EI_REPLACE_METHOD(0x005627A0, CParticleManager::DeletingDestruct);

void Terrain::Destruct() {
    ei::SetVtable(this, worlddata::kTerrainVtable);
    Terrain::Destroy();
    waterLevels.vtable = reinterpret_cast<void*>(kWaterLevelsVtable);
    waterLevels.FreeData();
    records::AsCString(textAE0).~CString();
    visibleColumnLast.FreeData();
    visibleColumnFirst.FreeData();
    wavePhases.FreeData();
    lights.Destroy();
    sectorCache.vtable = reinterpret_cast<void*>(kSectorCacheVtable);
    sectorCache.RemoveAll();
    vertexTiles.FreeData();
    archive.Destroy();
    textureFrames.FreeData();
    textureClass.FreeData();
    materials.FreeData();
    records::AsCString(textFC).~CString();
    records::AsCString(textF8).~CString();
    pageMaterials.FreeData();
    pageTextures.FreeData();
    ei::SetVtable(this, CTransformObject::kVtableAddress);
    CEngineObject::DestructBase();
}
EI_REPLACE_METHOD(0x00562B80, Terrain::Destruct);

Terrain* Terrain::DeletingDestruct(unsigned flags) {
    Destruct();
    return DeleteIf(this, flags);
}
EI_REPLACE_METHOD(0x00562B60, Terrain::DeletingDestruct);

void Sector::Destruct() {
    ei::SetVtable(this, kSectorVtable);
    Clear();
    occluders.vtable = reinterpret_cast<void*>(kSectorOccluderArrayVtable);
    occluders.FreeData();
    units.vtable = mfc::PtrListVtable();
    units.RemoveAll();
    objects.vtable = reinterpret_cast<void*>(kSectorObjectListVtable);
    objects.RemoveAll();
}
EI_REPLACE_METHOD(0x005BFCD0, Sector::Destruct);

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
    records::AsCString(texturePath_).~CString();
    platforms_.DestructBody();
    handle_.Destroy();
    scorchMarks_.Release<PrintElement>();
    footprints_.Release<PrintElement>();
    bloodPrints_.Release<PrintElement>();
    records::AsCString(zoneName_).~CString();
    GridResize2(&secondGrid_, nullptr, 1, 1);
    FreeGrid(secondGrid_);
    GridResize(&objectGrid_, nullptr, 1, 1);
    FreeGrid(objectGrid_);
    lineLists_.Destroy();
    reinterpret_cast<stlp::vector<ScreenEntry>*>(&hits_)->FreeStorage();
    screenEntries_.Destroy();
    dynamicNodes_.Destroy();
    dynamicObjects_.vtable = ObjectPtrListVtable();
    dynamicObjects_.RemoveAll();
    objects_.Destroy();
    DestroySectorList(&heroes_);
    countedA0_.vtable = reinterpret_cast<void*>(kCountedArrayVtable);
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
