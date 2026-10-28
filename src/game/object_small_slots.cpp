// Small virtual slots of the map objects' companions and the client controller. See unit.h,
// unit_client_models.h, map_object.h and docs/notes/map_objects.md.
#include "map_object.h"
#include "game_arrays.h"
#include "unit.h"
#include "unit_client_models.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"

namespace {
constexpr std::uint32_t kNoModel = 0xFFFFFFFFu;
constexpr int kDressedArmourSlots = 3;   // body, trousers, helmet
constexpr int kNoSound = -1;
} // namespace

bool CMapObject::ApplyScience(const LockKey*, CEngineObject*) {
    game::DebugLog("Science is applied on wrong object");
    return false;
}

void CMapObject::FreeArray(UseSpotArray* array, int) {
    lib::FreeStorage(*array);
}

int CMapObjectClientSpecific::Create() {
    lightStateA = 0;
    lightStateB = 0;
    particles.clear();
    endSound = kNoSound;
    owner = nullptr;
    return 1;
}

int CMapObjectServer::Create() {
    owner = nullptr;
    vitals.Reset();
    return 1;
}

void CMapObjectServer::Destroy() {
    vitals.Reset();
    strings.clear();
    owner = nullptr;
}

int CMapObject::CompanionQuery(int first, int second, int) {
    return companion->Query(first, second);
}

// The game's function reads armourIds[3..6] (+0xAC..+0xB8), the slots after the three dressed ones, not
// weaponIds: the name is provisional. Found when zone bz1g's units mismatched.
int CUnitClientSpecific::HasOtherArmour() {
    for (int slot = 3; slot < 7; ++slot)
        if (armourIds[slot] != kNoModel) return 1;
    return 0;
}

int CUnitClientSpecific::HasMainArmour() {
    for (int slot = 0; slot < kDressedArmourSlots; ++slot)
        if (armourIds[slot] != kNoModel) return 1;
    return 0;
}
