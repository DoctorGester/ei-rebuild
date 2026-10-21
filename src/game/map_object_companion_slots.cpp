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
#include "world.h"

namespace {
constexpr const char* kClientClassName = "CMapObjectClientSpecific";
constexpr const char* kServerClassName = "CMapObjectServer";
constexpr int kClientClassId = 0x3A;
constexpr int kServerClassId = 0x39;
constexpr std::uint32_t kShowsNumbers = 0x400000;   // CMapObject::flags_: hits show their number

// A particle source as the counted engine object it is (effects.h declares only its own members).
} // namespace

// --- The base companion -------------------------------------------------------------------------------

void CMapObjectCompanion::DestructCompanion() {
    ei::SetVtable(this, kVtableAddress);
    owner = nullptr;
    DestructBase();
}
EI_REPLACE_METHOD(0x00510730, CMapObjectCompanion::DestructCompanion);

CMapObjectCompanion* CMapObjectCompanion::DeleteCompanion(unsigned flags) {
    DestructCompanion();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00510710, CMapObjectCompanion::DeleteCompanion);

int CMapObjectCompanion::CreateCompanion() {
    owner = nullptr;
    return 1;
}
EI_REPLACE_METHOD(0x00510750, CMapObjectCompanion::CreateCompanion);
void CMapObjectCompanion::DestroyCompanion() { owner = nullptr; }
EI_REPLACE_METHOD(0x00510760, CMapObjectCompanion::DestroyCompanion);

int CMapObjectCompanion::UpdateCompanion() { return 0; }
EI_REPLACE_METHOD(0x005106E0, CMapObjectCompanion::UpdateCompanion);
// 005106D0 (slot 13 EffectsChanged; the server's too) and 00510700 (slot 19), RET 4: nothing.
void CMapObjectCompanion::IgnoreArgument(int) {}
EI_REPLACE_METHOD(0x005106D0, CMapObjectCompanion::IgnoreArgument);
EI_REPLACE_METHOD(0x00510700, CMapObjectCompanion::IgnoreArgument);
// 005104B0 (slot 14; the client's too) and the server's 00515810: nothing. One byte each (too short
// to hook).
void __fastcall CompanionNothing(CMapObjectCompanion*) {}
EI_REPLACE(0x005104B0, CompanionNothing);
EI_REPLACE(0x00515810, CompanionNothing);
// 005104C0 (slot 15 Query, RET 8; all three classes): 0.
int CMapObjectCompanion::QueryNothing(int, int) { return 0; }
EI_REPLACE_METHOD(0x005104C0, CMapObjectCompanion::QueryNothing);
// 005106F0 (slot 16 Hit, RET 8; the server's too): nothing.
void CMapObjectCompanion::HitNothing(int, int) {}
EI_REPLACE_METHOD(0x005106F0, CMapObjectCompanion::HitNothing);

// --- The client's and the server's ---------------------------------------------------------------------

CMapObjectClientSpecific* CMapObjectClientSpecific::DeleteClientCompanion(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00510540, CMapObjectClientSpecific::DeleteClientCompanion);

void CMapObjectClientSpecific::ShowHit(int damage, int kind) {
    if (ObjectSlots(owner)->IsDead() != 0) return;
    if (!(owner->flags_ & kShowsNumbers)) return;
    static_cast<CWorldClient*>(ManagerContext())->ShowNumber(owner->id_, damage, kind);
}
EI_REPLACE_METHOD(0x005117F0, CMapObjectClientSpecific::ShowHit);

// 00510520 / 00510530 (slots 11 / 12).
const char* __fastcall ClientCompanionClassName(CMapObjectClientSpecific*) {
    return kClientClassName;
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
    return kServerClassName;
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
    if (source) ++source->refCount_;
    if (old) old->Release();
    present = other->present;
    return this;
}
EI_REPLACE_METHOD(0x00512490, EffectParticle::Assign);

void EffectParticle::ReleaseSource() {
    if (source) source->Release();
}
EI_REPLACE_METHOD(0x00511290, EffectParticle::ReleaseSource);

// 00510340. A network record's destructor (the map object's destructor body, the record arrays'
// SetSize and the arrow effects call it): net_delta.h's, the four streams' destructor bodies.
EI_REPLACE_METHOD(0x00510340, DeltaField::Destruct);
