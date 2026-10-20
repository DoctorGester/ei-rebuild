// Small virtual slots of the map objects' companions and the client controller. See unit.h,
// unit_client_models.h, map_object.h and docs/notes/map_objects.md.
#include "map_object.h"
#include "game_arrays.h"
#include "unit.h"
#include "unit_client_models.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "originals_units.h"

namespace {
constexpr std::uint32_t kNoModel = 0xFFFFFFFFu;
constexpr int kDressedArmourSlots = 3;   // body, trousers, helmet
constexpr int kNoSound = -1;

// The MFC CArray layout slot 44 empties.
using RawArray = mfc::CArray<std::uint8_t>;
} // namespace

bool CMapObject::ApplyScienceImpl(int, int) {
    game::DebugLog("Science is applied on wrong object");
    return false;
}
EI_REPLACE_METHOD(0x0050F0F0, CMapObject::ApplyScienceImpl);

void CMapObject::FreeArray(void* array, int) {
    auto* raw = static_cast<RawArray*>(array);
    if (raw->data) {
        gamecrt::operator_delete(raw->data);
        raw->data = nullptr;
    }
    raw->maxSize = 0;
    raw->size = 0;
}
EI_REPLACE_METHOD(0x0050F120, CMapObject::FreeArray);

int CMapObjectClientSpecific::CreateClientCompanion() {
    lightStateA = 0;
    lightStateB = 0;
    mfc::impl::ArrayAs<gamearrays::Particles>(particles).SetSize(0, -1);
    endSound = kNoSound;
    owner = nullptr;
    return 1;
}
EI_REPLACE_METHOD(0x00510770, CMapObjectClientSpecific::CreateClientCompanion);

CUnitClientSpecific* CUnitClientSpecific::DeleteClient(std::uint32_t flags) {
    DestructClientBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00533E80, CUnitClientSpecific::DeleteClient);

int CMapObjectServer::CreateServer() {
    owner = nullptr;
    vitals.Reset();
    return 1;
}
EI_REPLACE_METHOD(0x005157C0, CMapObjectServer::CreateServer);

void CMapObjectServer::DestroyServer() {
    vitals.Reset();
    StringArraySetSize(&stringsVtable, nullptr, 0, -1);
    owner = nullptr;
}
EI_REPLACE_METHOD(0x005157E0, CMapObjectServer::DestroyServer);

int CMapObject::CompanionQuery(int first, int second, int) {
    return static_cast<CMapObjectCompanion*>(companion)->Query(first, second);
}
EI_REPLACE_METHOD(0x0050E460, CMapObject::CompanionQuery);

// 005391B0 reads armourIds[3..6] (+0xAC..+0xB8), the slots after the three dressed ones, not
// weaponIds (+0xBC): the name is provisional. Found when zone bz1g's units mismatched.
int CUnitClientSpecific::HasWeaponModels() {
    for (int slot = 3; slot < 7; ++slot)
        if (armourIds[slot] != kNoModel) return 1;
    return 0;
}
EI_REPLACE_METHOD(0x005391B0, CUnitClientSpecific::HasWeaponModels);

int CUnitClientSpecific::HasArmourModels() {
    for (int slot = 0; slot < kDressedArmourSlots; ++slot)
        if (armourIds[slot] != kNoModel) return 1;
    return 0;
}
EI_REPLACE_METHOD(0x005391E0, CUnitClientSpecific::HasArmourModels);
