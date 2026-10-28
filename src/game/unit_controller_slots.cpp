// Small virtual slots of a unit's controllers: the abstract base (the server's CUnitServer and the client's
// CUnitClientSpecific keep most of its empty slots), its lifetime and the client controller's small methods.
// Slot names: see CUnitController in unit.h.
#include "unit.h"
#include "unit_client_models.h"

#include "effects.h"   // CParticleSource
#include "texture.h"   // CTexture

#include "ei/hook.h"

#include <cstdint>

namespace {
constexpr const char* kClientClassName = "CUnitClientSpecific";
constexpr int kClientClassId = 0x3D;

// A marker source stopped, released and forgotten.
void DropMarker(CParticleSource*& marker) {
    if (!marker) return;
    marker->StopAndClear();
    marker->Release();
    marker = nullptr;
}
} // namespace

// --- The base controller's lifetime -----------------------------------------------------------------

CUnitController::~CUnitController() { unit = nullptr; }
EI_REWRITTEN_SPECIAL(0x00533DB0, CUnitController::~CUnitController);
EI_REWRITTEN_SPECIAL(0x00533D90, CUnitController::~CUnitController);

int CUnitController::Create() {
    unit = nullptr;
    return 1;
}
EI_REWRITTEN(0x00533DD0, CUnitController::Create);
void CUnitController::Destroy() { unit = nullptr; }
EI_REWRITTEN(0x00533DE0, CUnitController::Destroy);

// --- The base controller's empty slots ---------------------------------------------------------------
// The server's and the client's vtables share them as listed.

int CUnitController::FiguresLoaded() { return 1; }
EI_REWRITTEN(0x00533B90, CUnitController::FiguresLoaded);

// The client's too.
int CUnitController::RefreshVitals(char) { return 0; }
EI_REWRITTEN(0x00533BD0, CUnitController::RefreshVitals);
int CUnitController::Heal(float) { return 0; }
EI_REWRITTEN(0x00533BF0, CUnitController::Heal);

int CUnitController::TakeHit(DamageInfo*, CUnit*, CUnit*) { return 0; }
EI_REWRITTEN(0x00533BE0, CUnitController::TakeHit);

bool CUnitController::WeaponAdded(std::uint32_t) { return false; }
EI_REWRITTEN(0x00533C80, CUnitController::WeaponAdded);
bool CUnitController::RemoveWeaponModel(std::uint32_t) { return false; }
EI_REWRITTEN(0x00533C90, CUnitController::RemoveWeaponModel);

// --- The client controller -------------------------------------------------------------------------------

const char* CUnitClientSpecific::GetClassName() { return kClientClassName; }
EI_REWRITTEN(0x00533E00, CUnitClientSpecific::GetClassName);
int CUnitClientSpecific::GetClassId() { return kClientClassId; }
EI_REWRITTEN(0x00533E10, CUnitClientSpecific::GetClassId);

void CUnitClientSpecific::SetMarkers(CParticleSource* path, CParticleSource* marker) {
    DropMarker(sources[1]);
    DropMarker(sources[2]);
    if (path) {
        sources[1] = path;
        ++path->refCount_;
    }
    if (marker) {
        sources[2] = marker;
        ++marker->refCount_;
    }
}
EI_REWRITTEN(0x00535240, CUnitClientSpecific::SetMarkers);

void CUnitClientSpecific::DropFaceTexture() {
    CTexture* texture = faceTexture;
    faceTexture = nullptr;
    if (texture) texture->Release();
    blinks = 0;
}
EI_REWRITTEN(0x00538EA0, CUnitClientSpecific::DropFaceTexture);
