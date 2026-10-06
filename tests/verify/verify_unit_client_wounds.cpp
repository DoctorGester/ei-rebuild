// Checks for unit_client_wounds.cpp (A-U4).
// - WoundLevels (00536EA0): output. The original writes into a scratch six-byte buffer, ours into
//   the caller's; the six levels and the result (AL) compared.
// - ComposeWoundsTexture (005370F0): ours only (it writes the texture cache); without a wound the
//   result is 0, else what the texture manager returned (0 / 1).
// Reached by: RefreshWounds (00537360, the client half's slot 31) when a party unit's hit points
// change: combat (heroes hit), combat_more.
#include "verify.h"

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
