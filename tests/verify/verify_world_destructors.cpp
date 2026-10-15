// Checks for the world classes' compiler-written destructors (S13a, world_destructors.cpp). All
// ours only: a destructor frees what the original would free a second time. A body's wrapper
// checks the object after (CEngineObject's vtable back and no owner for the engine objects, the
// containers emptied); a deleting destructor's that it returns the object.
// Reached: the global worlds' exit functions (0058AAD0, 00561B00) and the start-up checks of
// verify_global_constructors.cpp, whose teardown deletes the particle manager and the terrain
// (slot 0); the AI map's sectors at every zone change and at exit (slot 0 with 0). The base body
// (0055EC80) runs inside ours for both worlds, so its own check counts no calls.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/effects.h"
#include "../../src/game/script.h"
#include "../../src/game/sector_map.h"
#include "../../src/game/terrain.h"
#include "../../src/game/world.h"

namespace {
void* s_unused;

bool EngineObjectGone(const CEngineObject* object) {
    return *reinterpret_cast<const std::uintptr_t*>(object) == CEngineObject::kVtableAddress && !object->owner_;
}

template <typename T>
bool ListEmpty(const mfc::CList<T>& list) {
    return list.count == 0 && !list.head && !list.tail && !list.free && !list.blocks;
}

void __fastcall WorldDestructVerify(CWorld* self) {
    self->Destruct();
    ei::verify::Check("CWorld::Destruct", true, EngineObjectGone(self), "world %p", static_cast<void*>(self));
}

void __fastcall ClientDestructVerify(CWorldClient* self) {
    self->Destruct();
    ei::verify::Check("CWorldClient::Destruct", true, EngineObjectGone(self), "world %p", static_cast<void*>(self));
}

// A destroyed script (S13b): every list empty, the arrays' vtables back.
template <typename T>
bool ArrayBack(const mfc::CArray<T>& array, std::uintptr_t vtable) {
    return reinterpret_cast<std::uintptr_t>(array.vtable) == vtable;
}
bool ScriptGone(const CScript* script) {
    return ListEmpty(script->tempObjects_) && ListEmpty(script->tempGroups_) && ListEmpty(script->worldBlocks_) &&
           ListEmpty(script->runs_) && ListEmpty(script->instances_) && ListEmpty(script->blocks_) &&
           ListEmpty(script->lightning_) && ListEmpty(script->fxSources_) && ListEmpty(script->particles_) &&
           ArrayBack(script->areas_, 0x0073D3C4) && ArrayBack(script->definitions_, 0x0073D3B8) &&
           ArrayBack(script->pointLights_, 0x0073D3A0);
}
bool AIMapGone(const CAIMap* map) {
    return reinterpret_cast<std::uintptr_t>(map->sectorMap.vtable) == CSectorMap::kVtableAddress &&
           ArrayBack(map->areaLists[0], 0x0073F9E0) &&
           reinterpret_cast<std::uintptr_t>(map->regionMap.local.cellRowsVtable) == kCellRowArrayVtable;
}

void __fastcall ScriptDestructVerify(CScript* self) {
    self->Destruct();
    ei::verify::Check("CScript::Destruct", true, ScriptGone(self), "script %p", static_cast<void*>(self));
}

void __fastcall AIMapDestructVerify(CAIMap* self) {
    DestructAIMap(self);
    ei::verify::Check("DestructAIMap", true, AIMapGone(self), "map %p", static_cast<void*>(self));
}

void __fastcall LocalSearchDestructVerify(LocalSearch* self) {
    self->Destruct();
    ei::verify::Check("LocalSearch::Destruct", true,
                      reinterpret_cast<std::uintptr_t>(self->cellRowsVtable) == kCellRowArrayVtable, "search %p",
                      static_cast<void*>(self));
}

void __fastcall SectorArrayDestructVerify(mfc::CArray<Sector, SectorElements>* self) {
    CSectorMap::DestructSectors(self);
    ei::verify::Check("CSectorMap::DestructSectors", true,
                      reinterpret_cast<std::uintptr_t>(self->vtable) == CSectorMap::kSectorArrayVtable, "array %p",
                      static_cast<void*>(self));
}

// The client's sector grids: the size taken, every row pointing into the block of empty lists.
bool GridIntact(const SectorGrid* grid, int width, int height) {
    if (grid->unknown08_ != width || grid->unknown0C_ != height || !grid->cells_) return false;
    if (reinterpret_cast<const int*>(grid->cells_)[-1] != width * height) return false;
    for (int y = 0; y < height; ++y)
        if (grid->rows_[y] != grid->cells_ + y * width) return false;
    return true;
}
void __fastcall GridAllocateVerify(SectorGrid* self) {
    self->Allocate();
    bool ok = GridIntact(self, self->unknown08_, self->unknown0C_);
    for (int i = 0; ok && i < self->unknown08_ * self->unknown0C_; ++i) ok = self->cells_[i].Empty();
    ei::verify::Check("SectorGrid::Allocate", true, ok, "grid %p", static_cast<void*>(self));
}
void __fastcall GridResizeVerify(SectorGrid* self, void*, int width, int height) {
    self->Resize(width, height);
    ei::verify::Check("SectorGrid::Resize", true, GridIntact(self, width, height), "grid %p, %d x %d",
                      static_cast<void*>(self), width, height);
}

void __fastcall ServerDestructVerify(CWorldServer* self) {
    self->Destruct();
    bool ok = EngineObjectGone(self) && ListEmpty(self->sharedObjects_) && ListEmpty(self->counted_) &&
              ListEmpty(self->projectiles_) && ScriptGone(self->MainScript()) && ScriptGone(self->SecondScript()) &&
              AIMapGone(self->AIMap());
    ei::verify::Check("CWorldServer::Destruct", true, ok, "world %p", static_cast<void*>(self));
}

void __fastcall ParticleManagerDestructVerify(CParticleManager* self) {
    self->Destruct();
    bool ok = ListEmpty(self->freeParticles) && self->held_.Size() == 0;
    ei::verify::Check("CParticleManager::Destruct", true, ok, "manager %p", static_cast<void*>(self));
}

void __fastcall TerrainDestructVerify(Terrain* self) {
    self->Destruct();
    bool ok = EngineObjectGone(self) && ListEmpty(self->sectorCache) && self->lights.Size() == 0;
    ei::verify::Check("Terrain::Destruct", true, ok, "terrain %p", static_cast<void*>(self));
}

void __fastcall SectorDestructVerify(Sector* self) {
    self->Destruct();
    bool ok = ListEmpty(self->objects) && ListEmpty(self->units) && self->freeOccluder == -1;
    ei::verify::Check("Sector::Destruct", true, ok, "sector %p", static_cast<void*>(self));
}

// A deleting destructor: the object back.
template <typename T>
T* CheckDelete(const char* name, T* self, unsigned flags) {
    T* result = self->DeletingDestruct(flags);
    ei::verify::Check(name, true, result == self, "object %p, flags %u", static_cast<void*>(self), flags);
    return result;
}
CWorld* __fastcall WorldDeleteVerify(CWorld* self, void*, unsigned flags) {
    return CheckDelete("CWorld::DeletingDestruct", self, flags);
}
CWorldClient* __fastcall ClientDeleteVerify(CWorldClient* self, void*, unsigned flags) {
    return CheckDelete("CWorldClient::DeletingDestruct", self, flags);
}
CWorldServer* __fastcall ServerDeleteVerify(CWorldServer* self, void*, unsigned flags) {
    return CheckDelete("CWorldServer::DeletingDestruct", self, flags);
}
CParticleManager* __fastcall ParticleManagerDeleteVerify(CParticleManager* self, void*, unsigned flags) {
    return CheckDelete("CParticleManager::DeletingDestruct", self, flags);
}
Terrain* __fastcall TerrainDeleteVerify(Terrain* self, void*, unsigned flags) {
    return CheckDelete("Terrain::DeletingDestruct", self, flags);
}
Sector* __fastcall SectorDeleteVerify(Sector* self, void*, unsigned flags) {
    return CheckDelete("Sector::DeletingDestruct", self, flags);
}

} // namespace

EI_VERIFY(0x0055EC80, WorldDestructVerify, 0, s_unused);
EI_VERIFY(0x00561B00, ClientDestructVerify, 0, s_unused);
EI_VERIFY(0x0058AAD0, ServerDestructVerify, 0, s_unused);
EI_VERIFY(0x005627C0, ParticleManagerDestructVerify, 0, s_unused);
EI_VERIFY(0x00562B80, TerrainDestructVerify, 0, s_unused);
EI_VERIFY(0x005BFCD0, SectorDestructVerify, 0, s_unused);
EI_VERIFY(0x0055EC60, WorldDeleteVerify, 0, s_unused);
EI_VERIFY(0x00561A00, ClientDeleteVerify, 0, s_unused);
EI_VERIFY(0x0058A430, ServerDeleteVerify, 0, s_unused);
EI_VERIFY(0x005627A0, ParticleManagerDeleteVerify, 0, s_unused);
EI_VERIFY(0x00562B60, TerrainDeleteVerify, 0, s_unused);
EI_VERIFY(0x0059E080, SectorDeleteVerify, 0, s_unused);
EI_VERIFY(0x0058A140, ScriptDestructVerify, 0, s_unused);
EI_VERIFY(0x0058A450, AIMapDestructVerify, 0, s_unused);
EI_VERIFY(0x0058AA60, LocalSearchDestructVerify, 0, s_unused);
EI_VERIFY(0x00599330, SectorArrayDestructVerify, 0, s_unused);
EI_VERIFY(0x00572820, GridAllocateVerify, 0, s_unused);
EI_VERIFY(0x005728D0, GridAllocateVerify, 0, s_unused);
EI_VERIFY(0x00572450, GridResizeVerify, 0, s_unused);
EI_VERIFY(0x005724F0, GridResizeVerify, 0, s_unused);
