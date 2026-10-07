// Checks for combat_object_slots.cpp (A-U5). Each changes shared state (the AI memory and
// options, references, the sector map), so only ours runs (stolenBytes 0) and the wrapper checks
// the state left behind.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/map_object.h"
#include "../../src/game/unit_ai.h"

#include <cstring>

namespace {
int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }

const AICallArray& CallsOf(const CUnitServer* self) { return reinterpret_cast<const AIBrain*>(self->brain)->calls; }
} // namespace

// 00552D00. Ours only. The mask joined the claims; the memory heard one more call, of kind 0x1E
// with the mask.
static void* s_unusedClaimHostility;
static void __fastcall ClaimHostilityVerify(CUnitServer* self, void*, CUnit* claimant, std::uint32_t mask) {
    int before = CallsOf(self).size;
    self->ClaimHostility(claimant, mask);
    const AICallArray& calls = CallsOf(self);
    bool added = calls.size == before + 1 && calls.data[before].kind == 0x1E &&
                 static_cast<std::uint32_t>(calls.data[before].unknown_10) == mask;
    ei::verify::Check("CUnitServer::ClaimHostility", true, added && (self->claimedHostile & mask) == mask,
                      "unit %d mask %08X: calls %d -> %d, claims %08X", UnitId(self), mask, before, calls.size,
                      self->claimedHostile);
}
EI_VERIFY(0x00552D00, ClaimHostilityVerify, 0, s_unusedClaimHostility);

// 00553610. Ours only (the AI options are rebuilt). The active and wanted weapon are the slot
// (result 1) or -1 (result 0).
static void* s_unusedTakeUpWeapon;
static BOOL __fastcall TakeUpWeaponVerify(CUnitServer* self, void*, int slot) {
    BOOL result = self->TakeUpWeapon(slot);
    int expected = result ? slot : -1;
    bool ok = static_cast<int>(self->unit->activeWeapon) == expected && self->unit->unknown_270 == expected;
    ei::verify::Check("CUnitServer::TakeUpWeapon", true, ok, "unit %d slot %d: result %d, active %d wanted %d",
                      UnitId(self), slot, result, static_cast<int>(self->unit->activeWeapon), self->unit->unknown_270);
    return result;
}
EI_VERIFY(0x00553610, TakeUpWeaponVerify, 0, s_unusedTakeUpWeapon);

// 00552670. Ours only (releases references). Afterwards the current order and the leader are not
// the object; a queued order still on it is an animation, or there is no current order.
static void* s_unusedOnObjectDeleted;
static void __fastcall OnObjectDeletedVerify(CUnitServer* self, void*, CEngineObject* object) {
    self->OnObjectDeleted(object);
    bool ok = !object || (self->action.target != object && self->followedLeader != object &&
                          (self->action2.target != object || self->action.kind == 0 || self->action2.kind == 7 ||
                           self->action2.kind == 8));
    ei::verify::Check("CUnitServer::OnObjectDeleted", true, ok, "unit %d object %d: orders %d / %d", UnitId(self),
                      object ? object->id_ : -1, self->action.kind, self->action2.kind);
}
EI_VERIFY(0x00552670, OnObjectDeletedVerify, 0, s_unusedOnObjectDeleted);

// 00553260. Ours only (the sector map, a possible hit). A living unit is back on the sector map.
static void* s_unusedResettle;
static void __fastcall ResettleVerify(CUnitServer* self) {
    bool dead = ObjectSlots(self->unit)->IsDead() != 0;
    self->Resettle();
    ei::verify::Check("CUnitServer::Resettle", true, dead || self->unit->sectorCellX != -1,
                      "unit %d: sector cell %d, %d", UnitId(self), self->unit->sectorCellX, self->unit->sectorCellY);
}
EI_VERIFY(0x00553260, ResettleVerify, 0, s_unusedResettle);

// 00546450. Ours only (resets the controller). Returns 1, no orders.
static void* s_unusedCreateServer;
static int __fastcall CreateServerVerify(CUnitServer* self) {
    int result = self->CreateServer();
    ei::verify::Check("CUnitServer::CreateServer", true, result == 1 && !self->action.target && !self->action2.target,
                      "result %d", result);
    return result;
}
EI_VERIFY(0x00546450, CreateServerVerify, 0, s_unusedCreateServer);

// 00546080. Ours only (ResetController's own wrapper is not involved: ours calls it directly).
static void* s_unusedResetSlot;
static void __fastcall ResetSlotVerify(CUnitServer* self) {
    self->ResetSlot();
    ei::verify::Check("CUnitServer::ResetSlot", true, !self->action.target && !self->action2.target, "unit %d",
                      UnitId(self));
}
EI_VERIFY(0x00546080, ResetSlotVerify, 0, s_unusedResetSlot);

// 005460A0: MOV EAX,[ECX+0x908] (6 bytes).
static int(__fastcall* s_tameStage)(CUnitServer*);
static int __fastcall TameStageVerify(CUnitServer* self) {
    int expected = s_tameStage(self);
    ei::verify::Check("CUnitServer::TameStage", expected, self->TameStage(), "unit %d", UnitId(self));
    return expected;
}
EI_VERIFY(0x005460A0, TameStageVerify, 6, s_tameStage);

// A-U7. 005460C0 / 005460D0: MOV EAX,imm32 (5 bytes); EAX compared. Every class-id check on a
// server unit (any scenario with units).
static const char*(__fastcall* s_serverClassName)(CUnitServer*);
static const char* __fastcall ServerClassNameVerify(CUnitServer* self) {
    const char* expected = s_serverClassName(self);
    ei::verify::Check("CUnitServer::ServerClassName", expected, self->ServerClassName(), "%p");
    return expected;
}
EI_VERIFY(0x005460C0, ServerClassNameVerify, 5, s_serverClassName);

static int(__fastcall* s_serverClassId)(CUnitServer*);
static int __fastcall ServerClassIdVerify(CUnitServer* self) {
    int expected = s_serverClassId(self);
    ei::verify::Check("CUnitServer::ServerClassId", expected, self->ServerClassId(), "%d");
    return expected;
}
EI_VERIFY(0x005460D0, ServerClassIdVerify, 5, s_serverClassId);

// 005460E0 (slot 0; a unit deleted: zone changes, world_load). Ours only; the memory may be gone
// afterwards: the result is this.
static void* s_unusedDeleteServer;
static CUnitServer* __fastcall DeleteServerVerify(CUnitServer* self, void*, unsigned flags) {
    CUnitServer* result = self->DeleteServer(flags);
    ei::verify::Check("CUnitServer::DeleteServer", true, result == self, "");
    return result;
}
EI_VERIFY(0x005460E0, DeleteServerVerify, 0, s_unusedDeleteServer);
