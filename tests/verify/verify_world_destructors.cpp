// Checks for the world classes' compiler-written destructors (S13a, world_destructors.cpp). All
// ours only: a destructor frees what the original would free a second time. A body's wrapper
// checks the object after (CEngineObject's vtable back and no owner for the engine objects, the
// containers emptied); a deleting destructor's that it returns the object.
// Reached: the global client world's exit function (00561B00) and the start-up checks of
// verify_global_constructors.cpp, whose teardown deletes the particle manager and the terrain
// (slot 0) and ends in the base body; the AI map's sectors at every zone change and at exit (slot
// 0 with 0).
#include "verify.h"

#include "../../src/game/effects.h"
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
EI_VERIFY(0x005627C0, ParticleManagerDestructVerify, 0, s_unused);
EI_VERIFY(0x00562B80, TerrainDestructVerify, 0, s_unused);
EI_VERIFY(0x005BFCD0, SectorDestructVerify, 0, s_unused);
EI_VERIFY(0x0055EC60, WorldDeleteVerify, 0, s_unused);
EI_VERIFY(0x00561A00, ClientDeleteVerify, 0, s_unused);
EI_VERIFY(0x005627A0, ParticleManagerDeleteVerify, 0, s_unused);
EI_VERIFY(0x00562B60, TerrainDeleteVerify, 0, s_unused);
EI_VERIFY(0x0059E080, SectorDeleteVerify, 0, s_unused);
