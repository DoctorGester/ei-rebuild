// Checks for taming (src/game/combat_taming.cpp): ours only (they change sides, players, AI
// modes and diplomacy), with checks that hold for any correct run.
#include "verify.h"

#include "../../src/game/combat.h"

namespace {
void* s_unused;
int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }
int Stage(const CUnitServer* self) { return *reinterpret_cast<const int*>(reinterpret_cast<const char*>(self) + 0x908); }
int TamerSide(const CUnitServer* self) { return *reinterpret_cast<const int*>(reinterpret_cast<const char*>(self) + 0x910); }
} // namespace

// 00551AE0: a success remembers the tamer's side and leaves a stage of 1..3.
static BOOL __fastcall TryTameVerify(CUnitServer* self, void*, CUnit* tamer, float roll) {
    BOOL result = self->TryTame(tamer, roll);
    bool ok = result == 0 || (result == 1 && TamerSide(self) == tamer->side && Stage(self) >= 1 && Stage(self) <= 3);
    ei::verify::Check("CUnitServer::TryTame", true, ok, "unit %d, result %d, stage %d", UnitId(self), result, Stage(self));
    return result;
}
EI_VERIFY(0x00551AE0, TryTameVerify, 0, s_unused);

// 00552280: stage 0 afterwards.
static void __fastcall UndoTamingVerify(CUnitServer* self) {
    self->UndoTaming();
    ei::verify::Check("CUnitServer::UndoTaming", 0, Stage(self), "unit %d", UnitId(self));
}
EI_VERIFY(0x00552280, UndoTamingVerify, 0, s_unused);
