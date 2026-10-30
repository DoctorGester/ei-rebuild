// The unit's link to its controller and player: the hostile mask, the motion rebuilt after a path
// change, the owning player, and the release / Destroy slots. See unit.h and docs/notes/units.md.
#include "ai_motion.h"
#include "effects.h"   // ParticleSource
#include "lib/crt.h"
#include "map_object.h"
#include "player_messages.h"
#include "texture.h"   // Texture
#include "unit.h"
#include "unit_client_models.h"

#include <cstdint>

namespace {
constexpr int kPartRelations = 11;                          // MarkChanged parts
constexpr int kPartPath = 10;

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
    Controller()->HostileMaskChanged();
    flags |= MapObjectFlag::RelationsChanged;
    MarkChanged(kPartRelations);
}

void Unit::RefreshMotion() {
    PathOrder* path = Controller()->Path();
    path->ForgetObject(id);
    float speed, turnRate;
    GetMoveSpeeds(&speed, &turnRate);
    UnitMotion* unitMotion = &motion;
    unitMotion->segment = -1;
    unitMotion->speedScale = speed;
    unitMotion->baseTurnRate = turnRate;
    unitMotion->Build(path);
    MarkChanged(kPartPath);
}

void Unit::SetPlayer(Player* newPlayer) {
    Player* old = player;
    if (old == newPlayer) return;
    player = newPlayer;
    if (old) old->RemoveUnit(this);
    if (player) player->AddUnit(this);
    MarkChanged(kPartRelations);
    if (controller) Controller()->PlayerChanged(newPlayer);
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
    DestroyMapObject();
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

bool UnitController::TakeHit(DamageInfo*, Unit*, Unit*) { return false; }

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
