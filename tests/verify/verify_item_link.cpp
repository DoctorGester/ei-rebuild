// Checks for item_link.cpp (A-W14): the item link record (CItemLink, vtable 0073FA88).
// - Construct 0058FBB0 (MOV EAX,ECX; MOV [EAX],imm32: 8 bytes) and Reset 0058FB90 (TEST ECX,ECX;
//   MOV [ECX],imm32: 8 bytes): the original on a scratch copy, ours on the record, the 12 bytes
//   compared.
// - Clone 0058FB60 (PUSH ESI; MOV ESI,ECX; PUSH 0xC: 5 bytes): both allocate a copy; the two
//   copies compared field by field (not by pointer), the original's freed afterwards.
// Reached: Clone and Reset only when a link is shared copy-on-share or shut down while shared (the
// counted pointer helpers 00418BE0 / 00413270); no scenario is known to do that. Construct has no
// caller left in the hooked game: CreateUnits and SyncPartyRecords are rewritten and inline it.
#include "verify.h"

#include "../../src/game/item.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {

CItemLink*(__fastcall* s_construct)(CItemLink*);
CItemLink* __fastcall ConstructVerify(CItemLink* self) {
    CItemLink scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    CItemLink* expected = s_construct(&scratch);
    CItemLink* result = self->Construct();
    bool ok = expected == &scratch && result == self && std::memcmp(&scratch, self, sizeof(scratch)) == 0;
    ei::verify::Check("CItemLink::Construct", true, ok, "link %p", self);
    return result;
}

void(__fastcall* s_reset)(CItemLink*);
void __fastcall ResetVerify(CItemLink* self) {
    CItemLink scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    s_reset(&scratch);
    self->Reset();
    ei::verify::Check("CItemLink::Reset", true, std::memcmp(&scratch, self, sizeof(scratch)) == 0, "link %p", self);
}

CItemLink*(__fastcall* s_clone)(CItemLink*);
CItemLink* __fastcall CloneVerify(CItemLink* self) {
    CItemLink* expected = s_clone(self);
    CItemLink* copy = self->Clone();
    bool ok = (expected == nullptr) == (copy == nullptr);
    if (ok && copy) ok = std::memcmp(expected, copy, sizeof(CItemLink)) == 0 && copy != self;
    ei::verify::Check("CItemLink::Clone", true, ok, "link %p value %u", self, self->value);
    if (expected) gamecrt::operator_delete(expected);
    return copy;
}

} // namespace

EI_VERIFY(0x0058FBB0, ConstructVerify, 8, s_construct);
EI_VERIFY(0x0058FB90, ResetVerify, 8, s_reset);
EI_VERIFY(0x0058FB60, CloneVerify, 5, s_clone);
