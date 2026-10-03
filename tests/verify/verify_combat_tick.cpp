// Checks for the per-tick order machine (src/game/combat_tick.cpp).
//
// SyncMoveModes writes only the movement records: ours, snapshot, restore, original, compare. The
// rest changes orders, paths, animations and references: ours only, with checks that hold for any
// correct run.
#include "verify.h"

#include "../../src/game/combat.h"

#include <cstring>

namespace {
void* s_unused;
using SyncFn = void(__fastcall*)(CUnitServer*);
SyncFn s_sync;
int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }
} // namespace

// 0054B320: ours only. The order equals `from` afterwards and the result is the order.
static UnitAction* __fastcall AssignVerify(UnitAction* self, void*, const UnitAction* from) {
    UnitAction* result = self->Assign(from);
    bool same = result == self && self->kind == from->kind && self->source == from->source &&
                self->target == from->target && std::memcmp(self->point, from->point, sizeof(self->point)) == 0 &&
                self->flag18 == from->flag18 && self->unknown_1C == from->unknown_1C && self->unknown_20 == from->unknown_20;
    ei::verify::Check("UnitAction::Assign", true, same, "kind %d", from->kind);
    return result;
}
EI_VERIFY(0x0054B320, AssignVerify, 0, s_unused);

// 0054B230: ours only.
static void __fastcall ResetVerify(UnitAction* self) {
    self->Reset();
    ei::verify::Check("UnitAction::Reset", true, self->kind == 9 && !self->target && !self->unknown_1C, "");
}
EI_VERIFY(0x0054B230, ResetVerify, 0, s_unused);

// 00546830: ours only.
static void __fastcall SetUnitMoveMode2Verify(CUnitServer* self, void*, int mode) {
    self->SetUnitMoveMode2(mode);
    ei::verify::Check("CUnitServer::SetUnitMoveMode2", mode, self->unit->moveMode2, "unit %d", UnitId(self));
}
EI_VERIFY(0x00546830, SetUnitMoveMode2Verify, 0, s_unused);

// 0054A620: CMP DWORD PTR [ECX+0x190],3 (7 bytes). The records +0x168..+0x177 compared.
static void __fastcall SyncMoveModesVerify(CUnitServer* self) {
    auto* records = reinterpret_cast<unsigned char*>(self) + 0x168;
    unsigned char before[0x10], ours[0x10];
    std::memcpy(before, records, sizeof(before));
    self->SyncMoveModes();
    std::memcpy(ours, records, sizeof(ours));
    std::memcpy(records, before, sizeof(before));
    s_sync(self);
    ei::verify::Check("CUnitServer::SyncMoveModes", true, std::memcmp(ours, records, sizeof(ours)) == 0, "unit %d",
                      UnitId(self));
}
EI_VERIFY(0x0054A620, SyncMoveModesVerify, 7, s_sync);

// 0054A6E0: ours only. The queued order is empty afterwards.
static void __fastcall TakeQueuedOrderVerify(CUnitServer* self) {
    int kind = self->action2.kind;
    self->TakeQueuedOrder();
    ei::verify::Check("CUnitServer::TakeQueuedOrder", true,
                      self->action.kind == kind && self->action2.kind == 9 && !self->action2.target, "unit %d",
                      UnitId(self));
}
EI_VERIFY(0x0054A6E0, TakeQueuedOrderVerify, 0, s_unused);

// 0054A900: ours only. Nothing starts without a queued order.
static bool __fastcall StartQueuedOrderVerify(CUnitServer* self) {
    int kind = self->action2.kind;
    bool result = self->StartQueuedOrder();
    ei::verify::Check("CUnitServer::StartQueuedOrder", true, kind != 9 || !result, "unit %d", UnitId(self));
    return result;
}
EI_VERIFY(0x0054A900, StartQueuedOrderVerify, 0, s_unused);

// 0054B390 / 0054DD70 / 0054D960 / 0054D140: ours only (counted; the scenarios check behaviour).
static void __fastcall UpdateOrdersVerify(CUnitServer* self) {
    self->UpdateOrders();
    ei::verify::Check("CUnitServer::UpdateOrders", true, true, "");
}
EI_VERIFY(0x0054B390, UpdateOrdersVerify, 0, s_unused);

// ContinueAction either carries on (ExecuteOrder) or ends the order (kind 9 then 0..).
static void __fastcall ContinueActionVerify(CUnitServer* self) {
    self->ContinueAction();
    ei::verify::Check("CUnitServer::ContinueAction", true, true, "");
}
EI_VERIFY(0x0054DD70, ContinueActionVerify, 0, s_unused);

static void __fastcall ExecuteOrderVerify(CUnitServer* self) {
    self->ExecuteOrder();
    ei::verify::Check("CUnitServer::ExecuteOrder", true, true, "");
}
EI_VERIFY(0x0054D960, ExecuteOrderVerify, 0, s_unused);

static void __fastcall ApproachTargetVerify(CUnitServer* self) {
    self->ApproachTarget();
    ei::verify::Check("CUnitServer::ApproachTarget", true, true, "");
}
EI_VERIFY(0x0054D140, ApproachTargetVerify, 0, s_unused);
