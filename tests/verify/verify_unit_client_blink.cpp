// Checks for unit_client_blink.cpp (A-U3). Ours only: the copies change texture pixels (a second
// run would copy the same pixels again, but through DirectDraw on the skin both share).
// - BlinkClose (00538ED0) / BlinkOpen (00539010): eyesClosed becomes 1 / 0 when a face and a skin
//   texture exist and the eyes were not already so (or `force`); otherwise it is unchanged.
// - BlitMipChain (00533980): runs (returns).
// Reached by: the client update's blink (005341A0, every 2..9 ticks) of units with a face texture
// (+0xCC): the party's heroes in every scenario that starts a game (units, combat, map_objects).
#include "verify.h"

#include "../../src/game/map_object_figures.h"
#include "../../src/game/map_object_shadow.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"

#include <windows.h>

namespace {
void* s_unusedClose;
void* s_unusedOpen;
void* s_unusedBlit;

bool HasSkin(CUnitClientSpecific* self) {
    auto* half = static_cast<CObject3DClientSpecific*>(self->unit->Object3D()->attached);
    return half->partSets[0] && half->partSets[0]->texture;
}

void CheckBlink(const char* name, CUnitClientSpecific* self, bool force, bool close) {
    int before = self->eyesClosed;
    bool copies = self->faceTexture && (force || before != (close ? 1 : 0)) && HasSkin(self);
    if (close) self->BlinkClose(force);
    else self->BlinkOpen(force);
    int expected = copies ? (close ? 1 : 0) : before;
    ei::verify::Check(name, expected, self->eyesClosed, "unit %d, before %d, force %d", self->unit->id_, before, force);
}
} // namespace

static void __fastcall BlinkCloseVerify(CUnitClientSpecific* self, void*, bool force) {
    CheckBlink("CUnitClientSpecific::BlinkClose", self, force, true);
}
EI_VERIFY(0x00538ED0, BlinkCloseVerify, 0, s_unusedClose);

static void __fastcall BlinkOpenVerify(CUnitClientSpecific* self, void*, bool force) {
    CheckBlink("CUnitClientSpecific::BlinkOpen", self, force, false);
}
EI_VERIFY(0x00539010, BlinkOpenVerify, 0, s_unusedOpen);

static void __fastcall BlitMipChainVerify(CTexture* target, const int* at, CTexture* source, const RECT* from) {
    BlitMipChain(target, at, source, from);
    ei::verify::Check("BlitMipChain", true, true, "at %d, %d", at[0], at[1]);
}
EI_VERIFY(0x00533980, BlitMipChainVerify, 0, s_unusedBlit);
