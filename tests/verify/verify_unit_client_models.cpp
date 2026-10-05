// Checks for unit_client_models.cpp.
//
// The part name collector and the skin layers only write their outputs: the original runs into
// fresh scratch containers, ours into the real ones, and what each produced is compared. Wearing
// armour adds and removes figure parts, and composing a skin creates a texture: ours only.
#include "verify.h"

#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"

#include "lib/crt.h"

#include <cstring>

namespace {
void* s_unused;

// Frees a scratch vector of strings the original filled.
void Discard(stlp::vector<stlp::string>& v) {
    v.clear();
    v.FreeStorage();
}

// Do the strings appended to `real` after `from` equal `scratch`, in order?
bool SameAppended(const stlp::vector<stlp::string>& real, std::size_t from, const stlp::vector<stlp::string>& scratch) {
    if (real.size() - from != scratch.size()) return false;
    for (std::size_t i = 0; i < scratch.size(); ++i)
        if (!(real.begin()[from + i] == scratch.begin()[i])) return false;
    return true;
}
} // namespace

// 00535CB0: PUSH -1; PUSH 0x71EB39 (7 bytes).
static void(__fastcall* s_collect)(const char*, const stlp::vector<std::uint32_t>*, const stlp::vector<std::uint32_t>*,
                                   stlp::vector<stlp::string>*, stlp::vector<stlp::string>*);
static void __fastcall CollectVerify(const char* prototype, const stlp::vector<std::uint32_t>* armourIds,
                                     const stlp::vector<std::uint32_t>* weaponIds, stlp::vector<stlp::string>* names,
                                     stlp::vector<stlp::string>* plainNames) {
    stlp::vector<stlp::string> scratchNames;
    stlp::vector<stlp::string> scratchPlain;
    s_collect(prototype, armourIds, weaponIds, &scratchNames, &scratchPlain);
    std::size_t namesBefore = names->size();
    std::size_t plainBefore = plainNames->size();
    CollectArmourPartNames(prototype, armourIds, weaponIds, names, plainNames);
    bool ok = SameAppended(*names, namesBefore, scratchNames) && SameAppended(*plainNames, plainBefore, scratchPlain);
    ei::verify::Check("CollectArmourPartNames", true, ok, "%u names (%u expected), %u plain (%u expected)",
                      static_cast<unsigned>(names->size() - namesBefore), static_cast<unsigned>(scratchNames.size()),
                      static_cast<unsigned>(plainNames->size() - plainBefore),
                      static_cast<unsigned>(scratchPlain.size()));
    Discard(scratchNames);
    Discard(scratchPlain);
}
EI_VERIFY(0x00535CB0, CollectVerify, 7, s_collect);

// 00536300: PUSH ECX; MOV EAX,[ESP+8]; PUSH EBX (6 bytes). The array is emptied first, so the
// original fills a fresh empty array (same grow-by); sizes and layers compared.
static void(__fastcall* s_buildSkinLayers)(CUnitClientSpecific*, void*, int, SkinLayerArray*);
static void __fastcall BuildSkinLayersVerify(CUnitClientSpecific* self, void*, int detailed, SkinLayerArray* out) {
    SkinLayerArray scratch{out->vtable, nullptr, 0, 0, out->growBy};
    s_buildSkinLayers(self, nullptr, detailed, &scratch);
    self->BuildSkinLayers(detailed, out);
    bool ok = scratch.size == out->size &&
              (scratch.size == 0 || std::memcmp(scratch.data, out->data, scratch.size * sizeof(SkinLayer)) == 0);
    ei::verify::Check("CUnitClientSpecific::BuildSkinLayers", true, ok, "unit %d: %d layers (%d expected)",
                      self->unit->id_, out->size, scratch.size);
    if (scratch.data) gamecrt::operator_delete(scratch.data);
}
EI_VERIFY(0x00536300, BuildSkinLayersVerify, 6, s_buildSkinLayers);

// 005381B0. Ours only (changes the figure): the slot of a valid armour piece then holds its id.
static bool __fastcall WearArmourModelVerify(CUnitClientSpecific* self, void*, std::uint32_t itemId) {
    bool result = self->WearArmourModel(itemId);
    bool ok = true;
    if (result) {
        bool found = false;
        for (std::uint32_t id : self->armourIds) found = found || id == itemId;
        ok = found;
    }
    ei::verify::Check("CUnitClientSpecific::WearArmourModel", true, ok, "item %08X", itemId);
    return result;
}
EI_VERIFY(0x005381B0, WearArmourModelVerify, 0, s_unused);

// 005385A0. Ours only (changes the figure): an invalid slot or id gives false and leaves the slots
// alone; a valid one is emptied (only that slot changes), and false then only comes from armour
// slots 0-2, the ones that look up the unit's figure.
static bool __fastcall RemoveArmourModelVerify(CUnitClientSpecific* self, void*, std::uint32_t slot) {
    std::uint32_t before[7];
    std::memcpy(before, self->armourIds, sizeof(before));
    bool result = self->RemoveArmourModel(slot);
    std::uint32_t id = slot < 7 ? before[slot] : 0xFFFFFFFFu;
    bool valid = id != 0xFFFFFFFFu && (id >> 16) != 0xFFFF && (id & 0xFFFF) != 0xFFFF;
    bool ok;
    if (!valid) {
        ok = !result && std::memcmp(before, self->armourIds, sizeof(before)) == 0;
    } else {
        before[slot] = 0xFFFFFFFFu;
        ok = std::memcmp(before, self->armourIds, sizeof(before)) == 0 && (result || slot <= 2);
    }
    ei::verify::Check("CUnitClientSpecific::RemoveArmourModel", true, ok, "slot %u item %08X result %d", slot, id,
                      result);
    return result;
}
EI_VERIFY(0x005385A0, RemoveArmourModelVerify, 0, s_unused);

namespace {
// The original's read-only lookups: 005169A0 (the unit's first figure part of a name, or null),
// 006A2B80 (a weapon's type by packed id).
__declspec(naked) CEngineObject* __fastcall FindAnyPartOriginal(CUnit*, void*, const char*) EI_ORIGINAL(0x005169A0)
__declspec(naked) int __fastcall WeaponTypeOriginal(std::uint32_t) EI_ORIGINAL(0x006A2B80)
} // namespace

// 00535590. Ours only (changes the figure): returns 1, the active weapon is `slot`, the byte +0x74
// is clear and the weapon ids are untouched; with a weapon in the new slot, +0x75 is set exactly
// when it is a bow whose figure has the part "rh3_arrow00", and that part then has flag 0x80;
// without one +0x75 keeps its value. Reached when a party unit's active weapon changes (the
// network state reader, a broken weapon dropped).
static int __fastcall ChangeWeaponModelVerify(CUnitClientSpecific* self, void*, std::uint32_t slot) {
    std::uint32_t weaponsBefore[4];
    std::memcpy(weaponsBefore, self->weaponIds, sizeof(weaponsBefore));
    std::uint8_t arrowBefore = reinterpret_cast<std::uint8_t*>(self)[0x75];
    int result = self->ChangeWeaponModel(slot);
    const std::uint8_t* bytes = reinterpret_cast<const std::uint8_t*>(self);
    std::uint32_t next = slot < 4 ? self->weaponIds[slot] : 0xFFFFFFFFu;
    const char* problem = nullptr;
    if (result != 1) problem = "result";
    else if (self->unit->activeWeapon != slot) problem = "active weapon";
    else if (bytes[0x74] != 0) problem = "+0x74 not cleared";
    else if (std::memcmp(weaponsBefore, self->weaponIds, sizeof(weaponsBefore)) != 0) problem = "weapon ids changed";
    else if (next == 0xFFFFFFFFu) {
        if (bytes[0x75] != arrowBefore) problem = "+0x75 changed without a weapon";
    } else {
        CEngineObject* arrow = WeaponTypeOriginal(next) == 5
                                   ? FindAnyPartOriginal(self->unit, nullptr, reinterpret_cast<const char*>(0x0078FAF8))
                                   : nullptr;
        if (bytes[0x75] != (arrow ? 1 : 0)) problem = "+0x75";
        else if (arrow && !(arrow->flags_ & 0x80)) problem = "arrow part not flagged";
    }
    ei::verify::Check("CUnitClientSpecific::ChangeWeaponModel", true, problem == nullptr, "unit %d slot %u weapon %08X: %s",
                      self->unit->id_, slot, next, problem ? problem : "");
    return result;
}
EI_VERIFY(0x00535590, ChangeWeaponModelVerify, 0, s_unused);

// 00536670. Ours only (creates a texture): counted.
static void* __fastcall ComposeSkinVerify(CUnitClientSpecific* self, void*, int detailed, void* target) {
    void* result = self->ComposeSkin(detailed, target);
    ei::verify::Check("CUnitClientSpecific::ComposeSkin", true, true, "");
    return result;
}
EI_VERIFY(0x00536670, ComposeSkinVerify, 0, s_unused);
