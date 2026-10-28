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
constexpr int kClientClassId = 0x3A;
constexpr int kServerClassId = 0x39;
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
const char* CMapObjectClientSpecific::GetClassName() { return kClientClassName; }
int CMapObjectClientSpecific::GetClassId() { return kClientClassId; }

// Class name and id.
const char* CMapObjectServer::GetClassName() { return kServerClassName; }
int CMapObjectServer::GetClassId() { return kServerClassId; }

// --- The client companion's particle records and the network records ---------------------------------

void EffectParticle::ReleaseSource() {
    if (source) source->Release();
}
