// Checks for unit_client_wounds.cpp (A-U4).
// - WoundLevels (00536EA0): output. The original writes into a scratch six-byte buffer, ours into
//   the caller's; the six levels and the result (AL) compared.
// - ComposeWoundsTexture (005370F0): ours only (it writes the texture cache); without a wound the
//   result is 0, else what the texture manager returned (0 / 1).
// Reached by: RefreshWounds (00537360, the client half's slot 31) when a party unit's hit points
// change: combat (heroes hit), combat_more.
#include "verify.h"

#include "../../src/game/map_object_shadow.h"
#include "../../src/game/maths.h"
#include "../../src/game/unit_client_models.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unusedComposeWounds;
} // namespace

// 00536EA0: PUSH ECX; PUSH EBX; PUSH EBP; MOV EBP,ECX (5 bytes).
static bool(__fastcall* s_woundLevels)(CUnitClientSpecific*, void*, std::uint8_t**);
static bool __fastcall WoundLevelsVerify(CUnitClientSpecific* self, void*, std::uint8_t** levels) {
    std::uint8_t scratch[8] = {};
    std::uint8_t* scratchData = scratch;
    bool expected = s_woundLevels(self, nullptr, &scratchData);
    bool actual = self->WoundLevels(levels);
    bool same = expected == actual && std::memcmp(scratch, *levels, 6) == 0;
    ei::verify::Check("CUnitClientSpecific::WoundLevels", true, same, "%d%d%d%d%d%d / %d%d%d%d%d%d, %d / %d", scratch[0],
                      scratch[1], scratch[2], scratch[3], scratch[4], scratch[5], (*levels)[0], (*levels)[1],
                      (*levels)[2], (*levels)[3], (*levels)[4], (*levels)[5], expected ? 1 : 0, actual ? 1 : 0);
    return actual;
}
EI_VERIFY(0x00536EA0, WoundLevelsVerify, 5, s_woundLevels);

static int __fastcall ComposeWoundsTextureVerify(CUnitClientSpecific* self) {
    int result = self->ComposeWoundsTexture();
    ei::verify::Check("CUnitClientSpecific::ComposeWoundsTexture", true, result == 0 || result == 1, "result %d", result);
    return result;
}
EI_VERIFY(0x005370F0, ComposeWoundsTextureVerify, 0, s_unusedComposeWounds);

// - LoadFaceTexture (00538DB0): ours only; true with a face texture whose flags_ has bit 8, false
//   without one. Reached by the client player's OnNewObject (00668330) for every new client unit
//   (heroes on every game start) and by the client world's 0056DE80.
namespace {
void* s_unusedLoadFace;
} // namespace

static bool __fastcall LoadFaceTextureVerify(CUnitClientSpecific* self) {
    bool result = self->LoadFaceTexture();
    bool ok = result ? self->faceTexture && (self->faceTexture->flags_ & 8) : !self->faceTexture;
    ei::verify::Check("CUnitClientSpecific::LoadFaceTexture", true, ok, "result %d", result ? 1 : 0);
    return result;
}
EI_VERIFY(0x00538DB0, LoadFaceTextureVerify, 0, s_unusedLoadFace);

// - HideWounds (0051C920): ours only; no wounds texture or material afterwards. Reached by
//   RefreshWounds (00537360) for a unit without wounds (combat, combat_more).
namespace {
void* s_unusedHideWounds;
} // namespace

static void __fastcall HideWoundsVerify(CObject3DClientSpecific* self) {
    self->HideWounds();
    ei::verify::Check("CObject3DClientSpecific::HideWounds", true, !self->woundsTexture && !self->woundsMaterial, "");
}
EI_VERIFY(0x0051C920, HideWoundsVerify, 0, s_unusedHideWounds);

// - ShowWounds (0051C3D0, A-U10): ours only (it loads the texture and makes the material). 1: the
//   wounds texture is held and the material's last stage draws it (its first the part set's
//   texture); 0: no first part set, or the texture and material dropped after a failed load.
//   Reached by RefreshWounds (00537360) for a wounded party unit (combat, combat_more).
namespace {
void* s_unusedShowWounds;
} // namespace

static int __fastcall ShowWoundsVerify(CObject3DClientSpecific* self, void*, const char* name) {
    int result = self->ShowWounds(name);
    bool ok;
    if (result == 1) {
        const WoundsMaterial* material = self->woundsMaterial;
        ok = self->woundsTexture && material && material->stages.size() >= 2 &&
             material->stages.back().texture == reinterpret_cast<CEngineObject*>(self->woundsTexture);
    } else {
        ok = result == 0 && (!self->material || (!self->woundsTexture && !self->woundsMaterial));
    }
    ei::verify::Check("CObject3DClientSpecific::ShowWounds", true, ok, "%s: %d", name, result);
    return result;
}
EI_VERIFY(0x0051C3D0, ShowWoundsVerify, 0, s_unusedShowWounds);

// - WeaponPoint (00535770): output. The original into a scratch vector (seeded with `out`), ours
//   into `out`; result and the three floats bitwise (copies). Reached by the original EffectPoint
//   (00532860) for another unit's weapon point (spells_cast, effects).
// 00535770: PUSH -1; PUSH 0071EA58 (7 bytes).
static bool(__fastcall* s_weaponPoint)(CUnitClientSpecific*, void*, Vector3*);
static bool __fastcall WeaponPointVerify(CUnitClientSpecific* self, void*, Vector3* out) {
    Vector3 scratch = *out;
    bool expected = s_weaponPoint(self, nullptr, &scratch);
    bool actual = self->WeaponPoint(out);
    bool same = expected == actual && std::memcmp(&scratch, out, sizeof(Vector3)) == 0;
    ei::verify::Check("CUnitClientSpecific::WeaponPoint", true, same, "%d / %d", expected ? 1 : 0, actual ? 1 : 0);
    return actual;
}
EI_VERIFY(0x00535770, WeaponPointVerify, 7, s_weaponPoint);
