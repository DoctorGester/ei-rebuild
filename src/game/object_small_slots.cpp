// Small virtual slots of the map objects' companions and the client controller. See unit.h,
// unit_client_models.h, map_object.h and docs/notes/map_objects.md.
#include "map_object.h"
#include "unit.h"
#include "unit_client_models.h"

#include "ei/hook.h"
#include "originals_library.h"

namespace {
constexpr std::uint32_t kNoModel = 0xFFFFFFFFu;
constexpr int kDressedArmourSlots = 3;   // body, trousers, helmet
} // namespace

int CMapObjectServer::CreateServer() {
    CompanionOwner(this) = nullptr;
    vitals.Reset();
    return 1;
}
EI_REPLACE_METHOD(0x005157C0, CMapObjectServer::CreateServer);

void CMapObjectServer::DestroyServer() {
    vitals.Reset();
    StringArraySetSize(&stringsVtable, nullptr, 0, -1);
    CompanionOwner(this) = nullptr;
}
EI_REPLACE_METHOD(0x005157E0, CMapObjectServer::DestroyServer);

int CMapObject::CompanionQuery(int first, int second, int) {
    return static_cast<CMapObjectCompanion*>(companion)->Query(first, second);
}
EI_REPLACE_METHOD(0x0050E460, CMapObject::CompanionQuery);

int CUnitClientSpecific::HasWeaponModels() {
    for (std::uint32_t id : weaponIds)
        if (id != kNoModel) return 1;
    return 0;
}
EI_REPLACE_METHOD(0x005391B0, CUnitClientSpecific::HasWeaponModels);

int CUnitClientSpecific::HasArmourModels() {
    for (int slot = 0; slot < kDressedArmourSlots; ++slot)
        if (armourIds[slot] != kNoModel) return 1;
    return 0;
}
EI_REPLACE_METHOD(0x005391E0, CUnitClientSpecific::HasArmourModels);
