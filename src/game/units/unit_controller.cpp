// The unit's link to its controller and player: the hostile mask, the motion rebuilt after a path
// change, the owning player, and the release / Destroy slots. See unit.h and docs/notes/units.md.
#include "game/ai/ai_motion.h"
#include "game/effects/effects.h"   // ParticleSource
#include "lib/crt.h"
#include "game/objects/map_object.h"
#include "game/players/player.h"
#include "engine/models/texture.h"
#include "game/units/unit.h"
#include "game/units/unit_client_models.h"

#include <cstdint>

namespace {

template <typename Item>
void ReleaseSlots(Item** slots, int count) {
    for (int i = 0; i < count; ++i) {
        if (slots[i]) slots[i]->Release();
        slots[i] = nullptr;
    }
}
} // namespace

void Unit::SetHostileMask(std::uint32_t mask) {
    relationsA = mask;
    controller->HostileMaskChanged();
    flags |= MapObjectFlag::RelationsChanged;
    MarkChanged(NetField::Relations);
}

void Unit::RefreshMotion() {
    PathOrder* path = controller->Path();
    path->ForgetObject(id);
    const MoveSpeeds speeds = GetMoveSpeeds();
    const float speed = speeds.forward, turnRate = speeds.turn;
    UnitMotion* unitMotion = &motion;
    unitMotion->segment = -1;
    unitMotion->speedScale = speed;
    unitMotion->baseTurnRate = turnRate;
    unitMotion->Build(path);
    MarkChanged(NetField::Path);
}

void Unit::SetPlayer(Player* newPlayer) {
    Player* old = player;
    if (old == newPlayer) return;
    player = newPlayer;
    if (old) old->RemoveUnit(this);
    if (player) player->AddUnit(this);
    MarkChanged(NetField::Relations);
    if (controller) controller->PlayerChanged(newPlayer);
}

void Unit::ReleaseUnit() {
    // The map object's virtual here is empty.
    SetPlayer(nullptr);
    if (controller) controller->ClearState();
    ReleaseSlots(quickItems, 4);
    ReleaseSlots(weapons, 4);
    ReleaseSlots(spells, 8);
    ReleaseSlots(armor, 7);
    for (Item* item : carried) item->Release();
    carried.clear();
}

void Unit::DestroyUnit() {
    ClearState();
    MapObject::Destroy();
    Reset();
}

// --- The controllers' small virtuals: the abstract base (the server's UnitServer and the client's
// UnitClientSpecific keep most of its empty slots), its lifetime and the client controller's small methods.

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

void UnitController::TakeHit(DamageInfo*, Unit*, Unit*) {}

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
