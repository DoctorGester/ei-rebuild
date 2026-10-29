// Small virtuals of a unit's controllers: the abstract base (the server's UnitServer and the client's
// UnitClientSpecific keep most of its empty slots), its lifetime and the client controller's small methods.
// Slot names: see UnitController in unit.h.
#include "effects.h"   // ParticleSource
#include "texture.h"   // CTexture
#include "unit.h"
#include "unit_client_models.h"

#include <cstdint>

namespace {
constexpr const char* kClientClassName = "CUnitClientSpecific";

// A marker source stopped, released and forgotten.
void DropMarker(ParticleSource*& marker) {
    if (!marker) return;
    marker->StopAndClear();
    marker->Release();
    marker = nullptr;
}
} // namespace

// --- The base controller's lifetime -----------------------------------------------------------------

UnitController::~UnitController() { unit = nullptr; }

void UnitController::Create() {
    unit = nullptr;
}
void UnitController::Destroy() { unit = nullptr; }

// --- The base controller's empty slots ---------------------------------------------------------------
// The server's and the client's vtables share them as listed.

int UnitController::FiguresLoaded() { return 1; }

// The client's too.
void UnitController::RefreshVitals(bool) {}
bool UnitController::Heal(float) { return false; }

bool UnitController::TakeHit(DamageInfo*, CUnit*, CUnit*) { return false; }

bool UnitController::WeaponAdded(std::uint32_t) { return false; }
bool UnitController::RemoveWeaponModel(std::uint32_t) { return false; }

// --- The client controller -------------------------------------------------------------------------------

const char* UnitClientSpecific::ClassName() { return kClientClassName; }
ClassId UnitClientSpecific::GetClassId() { return ClassId::UnitClientSpecific; }

void UnitClientSpecific::SetMarkers(ParticleSource* path, ParticleSource* marker) {
    DropMarker(sources[1]);
    DropMarker(sources[2]);
    if (path) {
        sources[1] = path;
        path->AddRef();
    }
    if (marker) {
        sources[2] = marker;
        marker->AddRef();
    }
}

void UnitClientSpecific::DropFaceTexture() {
    faceTexture.Reset();
    blinks = 0;
}
