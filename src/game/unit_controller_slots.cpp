// Small virtuals of a unit's controllers: the abstract base (the server's CUnitServer and the client's
// CUnitClientSpecific keep most of its empty slots), its lifetime and the client controller's small methods.
// Slot names: see CUnitController in unit.h.
#include "unit.h"
#include "unit_client_models.h"

#include "effects.h"   // CParticleSource
#include "texture.h"   // CTexture

#include <cstdint>

namespace {
constexpr const char* kClientClassName = "CUnitClientSpecific";

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

int CUnitController::Create() {
    unit = nullptr;
    return 1;
}
void CUnitController::Destroy() { unit = nullptr; }

// --- The base controller's empty slots ---------------------------------------------------------------
// The server's and the client's vtables share them as listed.

int CUnitController::FiguresLoaded() { return 1; }

// The client's too.
int CUnitController::RefreshVitals(char) { return 0; }
int CUnitController::Heal(float) { return 0; }

int CUnitController::TakeHit(DamageInfo*, CUnit*, CUnit*) { return 0; }

bool CUnitController::WeaponAdded(std::uint32_t) { return false; }
bool CUnitController::RemoveWeaponModel(std::uint32_t) { return false; }

// --- The client controller -------------------------------------------------------------------------------

const char* CUnitClientSpecific::ClassName() { return kClientClassName; }
ClassId CUnitClientSpecific::GetClassId() { return kClassUnitClientSpecific; }

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

void CUnitClientSpecific::DropFaceTexture() {
    CTexture* texture = faceTexture;
    faceTexture = nullptr;
    if (texture) texture->Release();
    blinks = 0;
}
