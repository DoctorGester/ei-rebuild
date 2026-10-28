// Create of map objects, units and torches: the map object's Create, then the object's number of network
// fields. See unit.h (CUnit::CreateUnit), torch.h (CTorchObject::CreateTorch) and docs/notes/units.md
// ("Creation, databases, save").
#include "map_object.h"
#include "game_strings.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "server_specific_manager.h"   // CTorchObjectSpec
#include "torch.h"
#include "unit.h"

#include "lib/crt.h"

#include <cstdint>

namespace {
constexpr int kUnitNetFields = 14;
constexpr int kTorchNetFields = 5;

constexpr int kMapObjectNetFields = 4;
constexpr int kInitialCountdown = 5;      // CMapObject::updateCountdown

void DetachCompanion(CMapObject* object) {
    if (CEngineObject* companion = object->companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    object->companion = nullptr;
}

} // namespace

// --- CMapObject's Create / Destroy (A-U) ---------------------------------------------------------

int CMapObject::CreateMapObject() {
    Create3D();
    DetachCompanion(this);
    packedVitals.Reset();
    animationSpeed = 1.0f;
    packedVitals.Reset();
    netState_.SetFieldCount(kMapObjectNetFields);
    netState_.Reset();
    changeStamp = -1;
    effects = {};
    updateCountdown = kInitialCountdown;
    name2 = game::kEmptyText;
    baseScale.x = baseScale.y = baseScale.z = 0.0f;
    return 1;
}

void CMapObject::DestroyMapObject() {
    SetEffectCount(0);
    DetachCompanion(this);
    Destroy3D();
}

// --- Units and torches -----------------------------------------------------------------------------

int CUnit::CreateUnit() {
    CreateMapObject();
    Reset();
    MapObject()->netState_.SetFieldCount(kUnitNetFields);
    return 1;
}

int CTorchObject::CreateTorch() {
    CreateMapObject();
    CTorchObjectSpec* old = spec;
    spec = nullptr;
    if (old) old->Release();
    DeltaState* state = &netState_;
    state->SetFieldCount(kTorchNetFields);
    state->Reset();
    return 1;
}
