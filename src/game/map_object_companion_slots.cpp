// Small virtual slots of a map object's companions (A-U12): the base companion (vtable 0073DC54;
// its slots are what the server's CMapObjectServer, 0073DD6C, mostly keeps), the client's
// CMapObjectClientSpecific (0073DBEC) and their deleting destructors. Slot names:
// CMapObjectCompanion (map_object.h).
#include "map_object.h"
#include "effects.h"       // CParticleSource
#include "net_delta.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_world.h"

namespace {
constexpr std::uintptr_t kCompanionVtable = std::uintptr_t(0x0073DC54);
constexpr std::uintptr_t kClientClassName = std::uintptr_t(0x0078EF9C);   // "CMapObjectClientSpecific"
constexpr std::uintptr_t kServerClassName = std::uintptr_t(0x0078F360);   // "CMapObjectServer"
constexpr int kClientClassId = 0x3A;
constexpr int kServerClassId = 0x39;
constexpr std::uint32_t kShowsNumbers = 0x400000;   // CMapObject::flags_: hits show their number

// A particle source as the counted engine object it is (effects.h declares only its own members).
CEngineObject* AsObject(CParticleSource* particleSource) { return reinterpret_cast<CEngineObject*>(particleSource); }
} // namespace

// --- The base companion -------------------------------------------------------------------------------

// 00510730. The destructor body: the base's vtable, no map object, the engine object's.
void __fastcall CompanionDestruct(CMapObjectCompanion* self) {
    ei::SetVtable(self, kCompanionVtable);
    CompanionOwner(self) = nullptr;
    self->DestructBase();
}
EI_REPLACE(0x00510730, CompanionDestruct);

// 00510710 (slot 0, RET 4).
CMapObjectCompanion* __fastcall CompanionDelete(CMapObjectCompanion* self, void*, unsigned flags) {
    CompanionDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x00510710, CompanionDelete);

// 00510750 (slot 2): no map object; 1. 00510760 (slot 3): no map object.
int __fastcall CompanionCreate(CMapObjectCompanion* self) {
    CompanionOwner(self) = nullptr;
    return 1;
}
EI_REPLACE(0x00510750, CompanionCreate);
void __fastcall CompanionDestroy(CMapObjectCompanion* self) { CompanionOwner(self) = nullptr; }
EI_REPLACE(0x00510760, CompanionDestroy);

// 005106E0 (slot 6, the per-tick update): 0.
int __fastcall CompanionUpdate(CMapObjectCompanion*) { return 0; }
EI_REPLACE(0x005106E0, CompanionUpdate);
// 005106D0 (slot 13 EffectsChanged; the server's too) and 00510700 (slot 19), RET 4: nothing.
void __fastcall CompanionIgnore(CMapObjectCompanion*, void*, int) {}
EI_REPLACE(0x005106D0, CompanionIgnore);
EI_REPLACE(0x00510700, CompanionIgnore);
// 005104B0 (slot 14; the client's too) and the server's 00515810: nothing. One byte each (too short
// to hook).
void __fastcall CompanionNothing(CMapObjectCompanion*) {}
EI_REPLACE(0x005104B0, CompanionNothing);
EI_REPLACE(0x00515810, CompanionNothing);
// 005104C0 (slot 15 Query, RET 8; all three classes): 0.
int __fastcall CompanionQuery(CMapObjectCompanion*, void*, int, int) { return 0; }
EI_REPLACE(0x005104C0, CompanionQuery);
// 005106F0 (slot 16 Hit, RET 8; the server's too): nothing.
void __fastcall CompanionHit(CMapObjectCompanion*, void*, int, int) {}
EI_REPLACE(0x005106F0, CompanionHit);

// --- The client's and the server's ---------------------------------------------------------------------

CMapObjectClientSpecific* CMapObjectClientSpecific::DeleteClientCompanion(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00510540, CMapObjectClientSpecific::DeleteClientCompanion);

void CMapObjectClientSpecific::ShowHit(int damage, int kind) {
    if (ObjectSlots(CompanionOwner(this))->IsDead() != 0) return;
    CMapObject* owner = CompanionOwner(this);
    if (!(owner->flags_ & kShowsNumbers)) return;
    ClientShowNumberOriginal(ManagerContext(), nullptr, owner->id_, damage, kind);
}
EI_REPLACE_METHOD(0x005117F0, CMapObjectClientSpecific::ShowHit);

// 00510520 / 00510530 (slots 11 / 12).
const char* __fastcall ClientCompanionClassName(CMapObjectClientSpecific*) {
    return reinterpret_cast<const char*>(kClientClassName);
}
EI_REPLACE(0x00510520, ClientCompanionClassName);
int __fastcall ClientCompanionClassId(CMapObjectClientSpecific*) { return kClientClassId; }
EI_REPLACE(0x00510530, ClientCompanionClassId);

CMapObjectServer* CMapObjectServer::DeleteServer(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00515700, CMapObjectServer::DeleteServer);

// 005156E0 / 005156F0 (slots 11 / 12).
const char* __fastcall ServerCompanionClassName(CMapObjectServer*) {
    return reinterpret_cast<const char*>(kServerClassName);
}
EI_REPLACE(0x005156E0, ServerCompanionClassName);
int __fastcall ServerCompanionClassId(CMapObjectServer*) { return kServerClassId; }
EI_REPLACE(0x005156F0, ServerCompanionClassId);

// --- The client companion's particle records and the network records ---------------------------------

EffectParticle* EffectParticle::Assign(const EffectParticle* other) {
    type = other->type;
    power = other->power;
    CParticleSource* old = source;
    source = other->source;
    if (source) ++AsObject(source)->refCount_;
    if (old) AsObject(old)->Release();
    present = other->present;
    return this;
}
EI_REPLACE_METHOD(0x00512490, EffectParticle::Assign);

void EffectParticle::ReleaseSource() {
    if (source) AsObject(source)->Release();
}
EI_REPLACE_METHOD(0x00511290, EffectParticle::ReleaseSource);

// 00510340. A network record's destructor (the map object's destructor body, the record arrays'
// SetSize and the arrow effects call it): net_delta.h's, the four streams' destructor bodies.
EI_REPLACE_METHOD(0x00510340, DeltaField::Destruct);
