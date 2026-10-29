// Small virtuals of the map objects' companions and the client controller. See unit.h,
// unit_client_models.h, map_object.h and docs/notes/map_objects.md.
#include "map_object.h"
#include "unit.h"
#include "unit_client_models.h"

#include "lib/crt.h"

namespace {
constexpr std::uint32_t kNoModel = 0xFFFFFFFFu;
constexpr int kDressedArmourSlots = 3;   // body, trousers, helmet
constexpr int kNoSound = -1;
} // namespace

bool CMapObject::ApplyScience(const LockKey*, CEngineObject*) {
    game::DebugLog("Science is applied on wrong object");
    return false;
}

void CMapObjectClientSpecific::Create() {
    lightStateA = 0;
    lightStateB = 0;
    particles.clear();
    endSound = kNoSound;
    owner = nullptr;
}

void CMapObjectServer::Create() {
    owner = nullptr;
    vitals.Reset();
}

void CMapObjectServer::Destroy() {
    vitals.Reset();
    strings.clear();
    owner = nullptr;
}

// The game's function reads armourIds[3..6] (+0xAC..+0xB8), the slots after the three dressed ones, not
// weaponIds: the name is provisional. Found when zone bz1g's units mismatched.
bool CUnitClientSpecific::HasOtherArmour() {
    for (int slot = 3; slot < 7; ++slot)
        if (armourIds[slot] != kNoModel) return true;
    return false;
}

bool CUnitClientSpecific::HasMainArmour() {
    for (int slot = 0; slot < kDressedArmourSlots; ++slot)
        if (armourIds[slot] != kNoModel) return true;
    return false;
}
