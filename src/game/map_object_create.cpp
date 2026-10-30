// Create of map objects, units and torches: the map object's Create, then the object's number of network
// fields. See unit.h (Unit::Create), torch.h (TorchObject::Create) and docs/notes/units.md
// ("Creation, databases, save").
#include "map_object.h"
#include "game_strings.h"
#include "map_object_figures.h"
#include "net_delta.h"
#include "server_specific_manager.h"   // TorchObjectSpec
#include "torch.h"
#include "unit.h"

#include "lib/crt.h"

#include <cstdint>

namespace {
constexpr int kUnitNetFields = 14;
constexpr int kTorchNetFields = 5;

constexpr int kMapObjectNetFields = 4;
constexpr int kInitialCountdown = 5;      // MapObject::updateCountdown

void DetachCompanion(MapObject* object) {
    if (EngineObject* companion = object->companion) {
        CompanionOwner(companion) = nullptr;
        companion->Release();
    }
    object->companion = nullptr;
}

} // namespace

// --- MapObject's Create / Destroy (A-U) ---------------------------------------------------------

void MapObject::CreateMapObject() {
    Create3D();
    DetachCompanion(this);
    packedVitals.Reset();
    animationSpeed = 1.0f;
    packedVitals.Reset();
    netState.SetFieldCount(kMapObjectNetFields);
    netState.Reset();
    changeStamp = -1;
    effects = {};
    updateCountdown = kInitialCountdown;
    name2 = game::kEmptyText;
    baseScale.x = baseScale.y = baseScale.z = 0.0f;
}

void MapObject::DestroyMapObject() {
    SetEffectCount(0);
    DetachCompanion(this);
    Destroy3D();
}

// --- Units and torches -----------------------------------------------------------------------------

void Unit::Create() {
    CreateMapObject();
    Reset();
    AsMapObject()->netState.SetFieldCount(kUnitNetFields);
}

void TorchObject::Create() {
    CreateMapObject();
    TorchObjectSpec* old = spec;
    spec = nullptr;
    if (old) old->Release();
    DeltaState* state = &netState;
    state->SetFieldCount(kTorchNetFields);
    state->Reset();
}
