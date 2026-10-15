// The destructors the compiler wrote for the world's classes (S13a): the bodies the exe's vtables
// reach through their slot 0 (the scalar deleting destructors), the members destroyed last to
// first after the class's own teardown. The containers are the library's (lib/mfc.h CArray /
// CList, lib/containers.h AlignedArray, lib/stlp.h hash_map). Checked by
// tests/verify/verify_world_destructors.cpp. docs/notes/standalone.md, S13a.
#include "effects.h"
#include "model.h"
#include "record_lifetime.h"
#include "sector_map.h"
#include "terrain.h"
#include "world.h"
#include "world_data.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

namespace {

// The members' vtables the destructors store back before emptying them.
constexpr std::uintptr_t kParticleListVtable = std::uintptr_t(0x0073D018);   // CTypedPtrList<CPtrList, Particle*>
constexpr std::uintptr_t kWaterLevelsVtable = std::uintptr_t(0x0073F730);    // CArray<CArray<float>>
constexpr std::uintptr_t kSectorCacheVtable = std::uintptr_t(0x0073F724);    // CList<SectorData*>

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
