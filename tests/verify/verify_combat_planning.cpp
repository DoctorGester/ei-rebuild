// Checks for the planning of an order's way (src/game/combat_planning.cpp).
//
// TimeUntilAction only reads (it simulates the unit's motion on a copy): ours and the original are
// compared. The rest search paths, lift units off the sector map and send messages: ours only,
// with checks that hold for any correct run.
#include "verify.h"

#include "../../src/game/combat.h"

namespace {
void* s_unused;
int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }
} // namespace

// 0054DF50: ours only. Only use orders (kind 6) can fail.
static bool __fastcall FindUseSpotVerify(CUnitServer* self, void*, UnitAction* order, void* motionOrder) {
    int kind = order->kind;
    bool result = self->FindUseSpot(order, motionOrder);
    ei::verify::Check("CUnitServer::FindUseSpot", true, result || kind == 6, "unit %d, kind %d", UnitId(self), kind);
    return result;
}
EI_VERIFY(0x0054DF50, FindUseSpotVerify, 0, s_unused);

// 0054E250: MOV EAX,FS:[0] (6 bytes). The time compared (steps of 5 ticks: exact).
using TimeFn = double(__fastcall*)(CUnitServer*, void*, const UnitAction*, void*);
static TimeFn s_time;
static double __fastcall TimeUntilActionVerify(CUnitServer* self, void*, const UnitAction* order, void* motionOrder) {
    double ours = self->TimeUntilAction(order, motionOrder);
    double theirs = s_time(self, nullptr, order, motionOrder);
    ei::verify::CheckNear("CUnitServer::TimeUntilAction", theirs, ours, "unit %d, order %d", UnitId(self), order->kind);
    return theirs;
}
EI_VERIFY(0x0054E250, TimeUntilActionVerify, 6, s_time);

// 0054E600: ours only. A unit without a player plans only use orders: anything else succeeds.
static bool __fastcall PlanOrderRouteVerify(CUnitServer* self, void*, UnitAction* order, std::uint32_t shout) {
    int kind = order->kind;
    bool party = self->unit->player != nullptr;
    bool result = self->PlanOrderRoute(order, shout);
    ei::verify::Check("CUnitServer::PlanOrderRoute", true, result || party || kind == 6, "unit %d, kind %d, result %d",
                      UnitId(self), kind, result);
    return result;
}
EI_VERIFY(0x0054E600, PlanOrderRouteVerify, 0, s_unused);

// 00554830: ours only. The unit's movement mode is the same afterwards.
static void __fastcall PlanMoveVerify(CUnitServer* self, void*, const float* point, int moving, int run, float range) {
    int mode = self->unit->moveMode;
    self->PlanMove(point, moving, run, range);
    ei::verify::Check("CUnitServer::PlanMove", mode, self->unit->moveMode, "unit %d", UnitId(self));
}
EI_VERIFY(0x00554830, PlanMoveVerify, 0, s_unused);
