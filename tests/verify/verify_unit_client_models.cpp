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

// 00536670. Ours only (creates a texture): counted.
static void* __fastcall ComposeSkinVerify(CUnitClientSpecific* self, void*, int detailed, void* target) {
    void* result = self->ComposeSkin(detailed, target);
    ei::verify::Check("CUnitClientSpecific::ComposeSkin", true, true, "");
    return result;
}
EI_VERIFY(0x00536670, ComposeSkinVerify, 0, s_unused);
