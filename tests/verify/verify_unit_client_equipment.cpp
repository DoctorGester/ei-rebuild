// Checks for unit_client_equipment.cpp (A-U).
// - AddWeaponModel (005388C0) / RemoveWeaponModel (00538920) change only the weapon ids: the ids
//   saved, the original runs, its result and ids kept, the ids put back, ours runs; compared.
// - SetArmourModels (00538140) / SetWeaponModels (00538860) change figure parts through the
//   controller's slots: ours only; afterwards every armour / weapon id shown is one of the first
//   min(count, 7 / 4) of `ids`.
// - RefreshSkins (00536100) / RefreshSimpleSkin (00536220) compose textures: ours only, result 1.
// Reached by: the client's network state reader (equipment ids, part 5: slots 32 / 33, then slot
// 29) for every unit with equipment at a zone load and after every change (units, unit_create,
// combat); ChangeWeaponModel's slot 30 (weapon changes: units, combat); slot 34 from slot 33. Slot
// 35 is a virtual slot whose callers are not known; its counts show whether a scenario reaches it.
#include "verify.h"

#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"

#include <cstring>

namespace {
void* s_unusedSetArmour;
void* s_unusedSetWeapons;
void* s_unusedRefresh;
void* s_unusedRefreshSimple;

bool AmongFirst(std::uint32_t id, const std::uint32_t* ids, int count, int limit) {
    if (id == 0xFFFFFFFFu) return true;
    int n = static_cast<std::uint32_t>(count) < static_cast<std::uint32_t>(limit) ? count : limit;
    for (int i = 0; i < n; ++i)
        if (ids[i] == id) return true;
    return false;
}
} // namespace

// 005388C0: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
static bool(__fastcall* s_addWeaponModel)(CUnitClientSpecific*, void*, std::uint32_t);
static bool __fastcall AddWeaponModelVerify(CUnitClientSpecific* self, void*, std::uint32_t id) {
    std::uint32_t before[4];
    std::memcpy(before, self->weaponIds, sizeof(before));
    bool expected = s_addWeaponModel(self, nullptr, id);
    std::uint32_t original[4];
    std::memcpy(original, self->weaponIds, sizeof(original));
    std::memcpy(self->weaponIds, before, sizeof(before));
    bool actual = self->AddWeaponModel(id);
    bool ok = expected == actual && std::memcmp(original, self->weaponIds, sizeof(original)) == 0;
    ei::verify::Check("CUnitClientSpecific::AddWeaponModel", true, ok, "id %08X: %d vs %d", id, expected ? 1 : 0,
                      actual ? 1 : 0);
    return actual;
}
EI_VERIFY(0x005388C0, AddWeaponModelVerify, 5, s_addWeaponModel);

// 00538920: PUSH ESI; MOV ESI,[ESP+8] (5 bytes).
static bool(__fastcall* s_removeWeaponModel)(CUnitClientSpecific*, void*, std::uint32_t);
static bool __fastcall RemoveWeaponModelVerify(CUnitClientSpecific* self, void*, std::uint32_t id) {
    std::uint32_t before[4];
    std::memcpy(before, self->weaponIds, sizeof(before));
    bool expected = s_removeWeaponModel(self, nullptr, id);
    std::uint32_t original[4];
    std::memcpy(original, self->weaponIds, sizeof(original));
    std::memcpy(self->weaponIds, before, sizeof(before));
    bool actual = self->RemoveWeaponModel(id);
    bool ok = expected == actual && std::memcmp(original, self->weaponIds, sizeof(original)) == 0;
    ei::verify::Check("CUnitClientSpecific::RemoveWeaponModel", true, ok, "id %08X: %d vs %d", id, expected ? 1 : 0,
                      actual ? 1 : 0);
    return actual;
}
EI_VERIFY(0x00538920, RemoveWeaponModelVerify, 5, s_removeWeaponModel);

static void __fastcall SetArmourModelsVerify(CUnitClientSpecific* self, void*, const std::uint32_t* ids, int count) {
    self->SetArmourModels(ids, count);
    bool ok = true;
    for (std::uint32_t id : self->armourIds) ok = ok && AmongFirst(id, ids, count, 7);
    ei::verify::Check("CUnitClientSpecific::SetArmourModels", true, ok, "unit %d, %d ids", self->unit->id_, count);
}
EI_VERIFY(0x00538140, SetArmourModelsVerify, 0, s_unusedSetArmour);

static void __fastcall SetWeaponModelsVerify(CUnitClientSpecific* self, void*, const std::uint32_t* ids, int count) {
    self->SetWeaponModels(ids, count);
    bool ok = true;
    for (std::uint32_t id : self->weaponIds) ok = ok && AmongFirst(id, ids, count, 4);
    ei::verify::Check("CUnitClientSpecific::SetWeaponModels", true, ok, "unit %d, %d ids", self->unit->id_, count);
}
EI_VERIFY(0x00538860, SetWeaponModelsVerify, 0, s_unusedSetWeapons);

static int __fastcall RefreshSkinsVerify(CUnitClientSpecific* self) {
    int result = self->RefreshSkins();
    ei::verify::Check("CUnitClientSpecific::RefreshSkins", 1, result, "unit %d", self->unit->id_);
    return result;
}
EI_VERIFY(0x00536100, RefreshSkinsVerify, 0, s_unusedRefresh);

static int __fastcall RefreshSimpleSkinVerify(CUnitClientSpecific* self) {
    int result = self->RefreshSimpleSkin();
    ei::verify::Check("CUnitClientSpecific::RefreshSimpleSkin", 1, result, "unit %d", self->unit->id_);
    return result;
}
EI_VERIFY(0x00536220, RefreshSimpleSkinVerify, 0, s_unusedRefreshSimple);
