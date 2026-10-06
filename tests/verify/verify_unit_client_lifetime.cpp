// Checks for unit_client_lifetime.cpp (A-U2). Both ours only (they release objects).
// - CreateClient (00533FE0): result 1; no sources or face texture, fully faded in, every model -1,
//   the footprint and sounds -1.
// - DestroyClient (005340C0): no sources or face texture, no blink.
// Reached by: every unit made on the client (units, unit_create, combat) and every unit removed
// (unit_create's remove, zone changes in zone_*).
#include "verify.h"

#include "../../src/game/unit.h"
#include "../../src/game/unit_client_models.h"

#include <cstdint>
#include <cstring>

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

// A-U4. DestructClientBody (00533EA0): ours only; afterwards no sources or face texture, the
// list +0x58 empty, the unit cleared and the engine object's base state (vtable 0073B8E0, no
// owner). Reached by DeleteClient (00533E80) when a party unit's controller goes (unit_create's
// remove, zone changes, game exit).
namespace {
void* s_unusedDestructBody;
} // namespace

static void __fastcall DestructClientBodyVerify(CUnitClientSpecific* self) {
    self->DestructClientBody();
    bool ok = !self->sources[0] && !self->sources[1] && !self->sources[2] && !self->faceTexture &&
              self->list58.count == 0 && !self->list58.head && !self->unit && !self->owner_ &&
              *reinterpret_cast<std::uintptr_t*>(self) == CEngineObject::kVtableAddress;
    ei::verify::Check("CUnitClientSpecific::DestructClientBody", true, ok, "");
}
EI_VERIFY(0x00533EA0, DestructClientBodyVerify, 0, s_unusedDestructBody);

// A-U4. ConstructClient (00533AD0): the original runs on a raw copy of the object's bytes (it
// allocates only the speech header, freed again), ours on the object; every byte compared except
// the header pointer (+0x9C), plus the header linked to itself. Reached by the object factory for
// every client unit controller (every game start).
// 00533AD0: PUSH -1; PUSH 0071E953 (7 bytes).
static CUnitClientSpecific*(__fastcall* s_constructClient)(CUnitClientSpecific*);
static CUnitClientSpecific* __fastcall ConstructClientVerify(CUnitClientSpecific* self) {
    alignas(CUnitClientSpecific) std::uint8_t scratch[sizeof(CUnitClientSpecific)];
    std::memcpy(scratch, self, sizeof(scratch));
    auto* expected = reinterpret_cast<CUnitClientSpecific*>(scratch);
    CUnitClientSpecific* original = s_constructClient(expected);
    CUnitClientSpecific* result = self->ConstructClient();
    const auto* ours = reinterpret_cast<const std::uint8_t*>(self);
    int first = -1;
    for (int i = 0; i < static_cast<int>(sizeof(scratch)) && first < 0; ++i) {
        if (i >= 0x9C && i < 0xA0) continue;
        if (scratch[i] != ours[i]) first = i;
    }
    bool ok = first < 0 && original == expected && result == self && self->speech->next == self->speech &&
              self->speech->prev == self->speech;
    stlp::deallocate(expected->speech, sizeof(ClientSpeechNode));
    ei::verify::Check("CUnitClientSpecific::ConstructClient", true, ok, "first differing byte +0x%X", first);
    return result;
}
EI_VERIFY(0x00533AD0, ConstructClientVerify, 7, s_constructClient);
