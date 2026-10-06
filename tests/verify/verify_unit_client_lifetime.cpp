// Checks for unit_client_lifetime.cpp (A-U2). Both ours only (they release objects).
// - CreateClient (00533FE0): result 1; no sources or face texture, fully faded in, every model -1,
//   the footprint and sounds -1.
// - DestroyClient (005340C0): no sources or face texture, no blink.
// Reached by: every unit made on the client (units, unit_create, combat) and every unit removed
// (unit_create's remove, zone changes in zone_*).
#include "verify.h"

#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"

namespace {
void* s_unusedCreate;
void* s_unusedDestroy;

bool NoModels(const CUnitClientSpecific* self) {
    for (std::uint32_t id : self->armourIds)
        if (id != 0xFFFFFFFFu) return false;
    for (std::uint32_t id : self->weaponIds)
        if (id != 0xFFFFFFFFu) return false;
    return true;
}
} // namespace

static int __fastcall CreateClientVerify(CUnitClientSpecific* self) {
    int result = self->CreateClient();
    bool ok = result == 1 && !self->sources[0] && !self->sources[1] && !self->sources[2] && !self->faceTexture &&
              self->fadeIn == 1.0f && NoModels(self) && self->footprint == -1 && self->footprintRepeats == -1 &&
              self->speechSound == -1 && self->idleSound == -1 && self->snapHeading == 1;
    ei::verify::Check("CUnitClientSpecific::CreateClient", true, ok, "result %d", result);
    return result;
}
EI_VERIFY(0x00533FE0, CreateClientVerify, 0, s_unusedCreate);

static void __fastcall DestroyClientVerify(CUnitClientSpecific* self) {
    self->DestroyClient();
    bool ok = !self->sources[0] && !self->sources[1] && !self->sources[2] && !self->faceTexture && self->blinks == 0;
    ei::verify::Check("CUnitClientSpecific::DestroyClient", true, ok, "");
}
EI_VERIFY(0x005340C0, DestroyClientVerify, 0, s_unusedDestroy);
