// Small virtuals of a map object's companions: the base companion (what the server's CMapObjectServer mostly
// keeps) and the client's CMapObjectClientSpecific. Virtual names: CMapObjectCompanion (map_object.h).
#include "map_object.h"
#include "effects.h"       // CParticleSource
#include "net_delta.h"
#include "unit.h"

#include "world.h"

namespace {
constexpr const char* kClientClassName = "CMapObjectClientSpecific";
constexpr const char* kServerClassName = "CMapObjectServer";
constexpr std::uint32_t kShowsNumbers = 0x400000;   // CMapObject::flags_: hits show their number

// A particle source as the counted engine object it is (effects.h declares only its own members).
} // namespace

// --- The base companion -------------------------------------------------------------------------------

CMapObjectCompanion::~CMapObjectCompanion() { owner = nullptr; }

int CMapObjectCompanion::Create() {
    owner = nullptr;
    return 1;
}
void CMapObjectCompanion::Destroy() { owner = nullptr; }

int CMapObjectCompanion::UpdateCompanion() { return 0; }
// Query (all three classes): 0.
int CMapObjectCompanion::Query(int, int) { return 0; }

// --- The client's and the server's ---------------------------------------------------------------------

void CMapObjectClientSpecific::Hit(int damage, int kind) {
    if (owner->IsDead() != 0) return;
    if (!(owner->flags_ & kShowsNumbers)) return;
    ClientWorld()->ShowNumber(owner->id_, damage, kind);
}

// Class name and id.
const char* CMapObjectClientSpecific::ClassName() { return kClientClassName; }
ClassId CMapObjectClientSpecific::GetClassId() { return kClassMapObjectClientSpecific; }

// Class name and id.
EffectParticle::EffectParticle(EffectParticle&&) noexcept = default;
EffectParticle& EffectParticle::operator=(EffectParticle&&) noexcept = default;
EffectParticle::~EffectParticle() = default;

const char* CMapObjectServer::ClassName() { return kServerClassName; }
ClassId CMapObjectServer::GetClassId() { return kClassMapObjectServer; }

// --- The client companion's particle records and the network records ---------------------------------

