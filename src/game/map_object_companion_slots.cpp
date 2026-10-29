// Small virtuals of a map object's companions: the base companion (what the server's MapObjectServer mostly
// keeps) and the client's MapObjectClientSpecific. Virtual names: MapObjectCompanion (map_object.h).
#include "effects.h"   // ParticleSource
#include "map_object.h"
#include "net_delta.h"
#include "unit.h"
#include "world.h"

namespace {
constexpr const char* kClientClassName = "CMapObjectClientSpecific";
constexpr const char* kServerClassName = "CMapObjectServer";

} // namespace

// --- The base companion -------------------------------------------------------------------------------

MapObjectCompanion::~MapObjectCompanion() { owner = nullptr; }

void MapObjectCompanion::Create() {
    owner = nullptr;
}
void MapObjectCompanion::Destroy() { owner = nullptr; }

// --- The client's and the server's ---------------------------------------------------------------------

void MapObjectClientSpecific::Hit(int damage, int kind) {
    if (owner->IsDead() != 0) return;
    if (!(owner->flags & MapObjectFlag::InParty)) return;
    ClientWorld()->ShowNumber(owner->id, damage, kind);
}

// Class name and id.
const char* MapObjectClientSpecific::ClassName() { return kClientClassName; }
ClassId MapObjectClientSpecific::GetClassId() { return ClassId::MapObjectClientSpecific; }

// Class name and id.
EffectParticle::EffectParticle(EffectParticle&&) noexcept = default;
EffectParticle& EffectParticle::operator=(EffectParticle&&) noexcept = default;
EffectParticle::~EffectParticle() = default;

const char* MapObjectServer::ClassName() { return kServerClassName; }
ClassId MapObjectServer::GetClassId() { return ClassId::MapObjectServer; }

// --- The client companion's particle records and the network records ---------------------------------

