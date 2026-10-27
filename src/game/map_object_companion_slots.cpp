// Small virtuals of a map object's companions: the base companion (what the server's CMapObjectServer mostly
// keeps) and the client's CMapObjectClientSpecific. Virtual names: CMapObjectCompanion (map_object.h).
#include "map_object.h"
#include "effects.h"       // CParticleSource
#include "net_delta.h"
#include "unit.h"

#include "ei/hook.h"
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

CMapObjectCompanion::~CMapObjectCompanion() { owner = nullptr; }
EI_REWRITTEN_SPECIAL(0x00510730, CMapObjectCompanion::~CMapObjectCompanion);
EI_REWRITTEN_SPECIAL(0x00510710, CMapObjectCompanion::~CMapObjectCompanion);

int CMapObjectCompanion::Create() {
    owner = nullptr;
    return 1;
}
EI_REWRITTEN(0x00510750, CMapObjectCompanion::Create);
void CMapObjectCompanion::Destroy() { owner = nullptr; }
EI_REWRITTEN(0x00510760, CMapObjectCompanion::Destroy);

int CMapObjectCompanion::UpdateCompanion() { return 0; }
EI_REPLACE_METHOD(0x005106E0, CMapObjectCompanion::UpdateCompanion);
// EffectsChanged (the server's too) and the visibility virtual: nothing.
void CMapObjectCompanion::IgnoreArgument(int) {}
EI_REPLACE_METHOD(0x005106D0, CMapObjectCompanion::IgnoreArgument);
EI_REPLACE_METHOD(0x00510700, CMapObjectCompanion::IgnoreArgument);
// The unused virtual (the client's too) and the server's: nothing. One byte each (too short to hook).
void __fastcall CompanionNothing(CMapObjectCompanion*) {}
EI_REPLACE(0x005104B0, CompanionNothing);
EI_REPLACE(0x00515810, CompanionNothing);
// Query (all three classes): 0.
int CMapObjectCompanion::Query(int, int) { return 0; }
EI_REWRITTEN(0x005104C0, CMapObjectCompanion::Query);
// Hit (the server's too): nothing.
void CMapObjectCompanion::HitNothing(int, int) {}
EI_REPLACE_METHOD(0x005106F0, CMapObjectCompanion::HitNothing);

// --- The client's and the server's ---------------------------------------------------------------------

void CMapObjectClientSpecific::Hit(int damage, int kind) {
    if (ObjectSlots(owner)->IsDead() != 0) return;
    if (!(owner->flags_ & kShowsNumbers)) return;
    ClientWorld()->ShowNumber(owner->id_, damage, kind);
}
EI_REWRITTEN(0x005117F0, CMapObjectClientSpecific::Hit);

// Class name and id.
const char* CMapObjectClientSpecific::GetClassName() { return kClientClassName; }
EI_REWRITTEN(0x00510520, CMapObjectClientSpecific::GetClassName);
int CMapObjectClientSpecific::GetClassId() { return kClientClassId; }
EI_REWRITTEN(0x00510530, CMapObjectClientSpecific::GetClassId);

// Class name and id.
const char* CMapObjectServer::GetClassName() { return kServerClassName; }
EI_REWRITTEN(0x005156E0, CMapObjectServer::GetClassName);
int CMapObjectServer::GetClassId() { return kServerClassId; }
EI_REWRITTEN(0x005156F0, CMapObjectServer::GetClassId);

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
EI_REWRITTEN(0x00512490, EffectParticle::Assign);

void EffectParticle::ReleaseSource() {
    if (source) source->Release();
}
EI_REWRITTEN(0x00511290, EffectParticle::ReleaseSource);
