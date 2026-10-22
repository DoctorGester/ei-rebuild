// Small virtual slots of a map object's companions (A-U12): the base companion (game.exe's vtable
// 0073DC54; its slots are what the server's CMapObjectServer, 0073DD6C, mostly keeps), the client's
// CMapObjectClientSpecific (0073DBEC), their deleting destructors, and the three compiled tables (C19).
// Slot names: CMapObjectCompanion (map_object.h).
#include "map_object.h"
#include "effects.h"       // CParticleSource
#include "engine_object_class.h"
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
    ei::SetVtable(this, BaseVtable());
    owner = nullptr;
    DestructBase();
}
EI_REPLACE_METHOD(0x00510730, CMapObjectCompanion::DestructCompanion);

CMapObjectCompanion* CMapObjectCompanion::DeletingDestruct(unsigned flags) {
    DestructCompanion();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00510710, CMapObjectCompanion::DeletingDestruct);

int CMapObjectCompanion::CreateCompanion() {
    owner = nullptr;
    return 1;
}
EI_REWRITTEN(0x00510750, CMapObjectCompanion::CreateCompanion);
void CMapObjectCompanion::DestroyCompanion() { owner = nullptr; }
EI_REWRITTEN(0x00510760, CMapObjectCompanion::DestroyCompanion);

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
EI_REWRITTEN(0x005104C0, CMapObjectCompanion::QueryNothing);
// 005106F0 (slot 16 Hit, RET 8; the server's too): nothing.
void CMapObjectCompanion::HitNothing(int, int) {}
EI_REPLACE_METHOD(0x005106F0, CMapObjectCompanion::HitNothing);

// --- The client's and the server's ---------------------------------------------------------------------

CMapObjectClientSpecific* CMapObjectClientSpecific::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00510540, CMapObjectClientSpecific::DeletingDestruct);

void CMapObjectClientSpecific::ShowHit(int damage, int kind) {
    if (ObjectSlots(owner)->IsDead() != 0) return;
    if (!(owner->flags_ & kShowsNumbers)) return;
    ClientWorld()->ShowNumber(owner->id_, damage, kind);
}
EI_REWRITTEN(0x005117F0, CMapObjectClientSpecific::ShowHit);

// 00510520 / 00510530 (slots 11 / 12).
const char* __fastcall ClientCompanionClassName(CMapObjectClientSpecific*) {
    return kClientClassName;
}
EI_REWRITTEN(0x00510520, ClientCompanionClassName);
int __fastcall ClientCompanionClassId(CMapObjectClientSpecific*) { return kClientClassId; }
EI_REWRITTEN(0x00510530, ClientCompanionClassId);

CMapObjectServer* CMapObjectServer::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00515700, CMapObjectServer::DeletingDestruct);

// 005156E0 / 005156F0 (slots 11 / 12).
const char* __fastcall ServerCompanionClassName(CMapObjectServer*) {
    return kServerClassName;
}
EI_REWRITTEN(0x005156E0, ServerCompanionClassName);
int __fastcall ServerCompanionClassId(CMapObjectServer*) { return kServerClassId; }
EI_REWRITTEN(0x005156F0, ServerCompanionClassId);

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

// --- The compiled tables (C19) -------------------------------------------------------------------------
// Compiled tags (engine_object_class.h) in place of game.exe's 0073DC54 (the base companion), 0073DBEC
// (CMapObjectClientSpecific) and 0073DD6C (CMapObjectServer, one slot more: 20 TakeHit; its slot 14, 00515810, is
// another copy of the base's empty body). CompanionClass declares CMapObjectCompanion's slots 13-19 (map_object.h)
// with the base's bodies; each companion's tag overrides the slots its game.exe table changes with the bodies that
// table reached (qualified: no dispatch). Slot 19's originals set no result (the objects' slot 47 passes EAX on),
// so it is void.

namespace {

template <typename Self, typename T>
class CompanionClass : public EngineObjectClass<Self, T> {
public:
    int Create() override { return Companion()->CreateCompanion(); }   // 2: 00510750
    void Destroy() override { Companion()->DestroyCompanion(); }        // 3: 00510760
    int RunTick() override { return Companion()->UpdateCompanion(); }   // 6: 005106E0

    virtual void EffectsChanged(bool) {}                     // 13: 005106D0 (RET 4)
    virtual void Unused14() { CompanionNothing(Companion()); }   // 14: 005104B0
    virtual int Query(int first, int second) { return Companion()->QueryNothing(first, second); }   // 15: 005104C0
    virtual void Hit(int damage, int kind) { Companion()->HitNothing(damage, kind); }   // 16: 005106F0
    virtual int IsDead() { return _purecall(); }             // 17: pure
    virtual int IsOverkilled() { return _purecall(); }       // 18: pure
    virtual void SetLightMode(int) {}                        // 19: 00510700 (RET 4)

protected:
    CMapObjectCompanion* Companion() { return this->template As<CMapObjectCompanion>(); }
};

class CompanionBaseClass final : public CompanionClass<CompanionBaseClass, CMapObjectCompanion> {};

class CMapObjectClientSpecificClass final : public CompanionClass<CMapObjectClientSpecificClass, CMapObjectClientSpecific> {
public:
    int Create() override { return Client()->CreateClientCompanion(); }   // 2: 00510770
    void Destroy() override { Client()->DestroyClientCompanion(); }       // 3: 005107B0
    int RunTick() override { return Client()->Update(); }                 // 6: 00510890
    const char* ClassName() override { return ClientCompanionClassName(Client()); }   // 11: 00510520
    int ClassId() override { return ClientCompanionClassId(Client()); }               // 12: 00510530
    void EffectsChanged(bool first) override { Client()->OnEffectsChanged(first); }   // 13: 005117D0
    void Hit(int damage, int kind) override { Client()->ShowHit(damage, kind); }      // 16: 005117F0
    int IsDead() override { return Client()->CMapObjectClientSpecific::IsDead(); }              // 17: 005104D0
    int IsOverkilled() override { return Client()->CMapObjectClientSpecific::IsOverkilled(); }  // 18: 005104F0
    void SetLightMode(int mode) override { Client()->ApplyLightMode(mode); }          // 19: 00511A00

private:
    CMapObjectClientSpecific* Client() { return As<CMapObjectClientSpecific>(); }
};

class CMapObjectServerClass final : public CompanionClass<CMapObjectServerClass, CMapObjectServer> {
public:
    int Create() override { return Server()->CreateServer(); }   // 2: 005157C0
    void Destroy() override { Server()->DestroyServer(); }       // 3: 005157E0
    int RunTick() override { return Server()->Update(); }        // 6: 00515820
    void Load(CStream* stream) override { Server()->LoadServer(stream); }   // 9: 005159D0
    void Save(CStream* stream) override { Server()->SaveServer(stream); }   // 10: 00515AC0
    const char* ClassName() override { return ServerCompanionClassName(Server()); }   // 11: 005156E0
    int ClassId() override { return ServerCompanionClassId(Server()); }               // 12: 005156F0
    int IsDead() override { return Server()->CMapObjectServer::IsDead(); }              // 17: 005156A0
    int IsOverkilled() override { return Server()->CMapObjectServer::IsOverkilled(); }  // 18: 005156C0
    virtual BOOL TakeHit(const void* hit) { return Server()->TakeHit(hit); }            // 20: 00515980

private:
    CMapObjectServer* Server() { return As<CMapObjectServer>(); }
};

} // namespace

void* CMapObjectCompanion::BaseVtable() { return CompanionBaseClass::Vtable(); }
void* CMapObjectClientSpecific::Vtable() { return CMapObjectClientSpecificClass::Vtable(); }
void* CMapObjectServer::Vtable() { return CMapObjectServerClass::Vtable(); }
