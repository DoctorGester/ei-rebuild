// Checks for map_object_vitals.cpp (A-U5).
#include "verify.h"

#include "../../src/game/map_object.h"
#include "../../src/game/unit.h"

#include <cstring>

// 0050F080: PUSH ESI; MOV ESI,ECX; MOV ECX,[ESI+0x170] (9 bytes). Ours, the companion's
// protection kept and restored, the original; both must leave the same bytes.
static void(__fastcall* s_clearProtectionBases)(CMapObject*);
static void __fastcall ClearProtectionBasesVerify(CMapObject* self) {
    if (self->companion->GetClassId() != 0x39) {
        self->ClearProtectionBases();
        return;
    }
    UnitStatPair* protection = static_cast<CMapObjectServer*>(self->companion)->vitals.protection;
    UnitStatPair saved[7];
    UnitStatPair ours[7];
    std::memcpy(saved, protection, sizeof(saved));
    self->ClearProtectionBases();
    std::memcpy(ours, protection, sizeof(ours));
    std::memcpy(protection, saved, sizeof(saved));
    s_clearProtectionBases(self);
    ei::verify::Check("CMapObject::ClearProtectionBases", true, std::memcmp(ours, protection, sizeof(ours)) == 0,
                      "object %d", self->id_);
}
EI_VERIFY(0x0050F080, ClearProtectionBasesVerify, 9, s_clearProtectionBases);
