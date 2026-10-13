// Checks for combat_order_checks.cpp (A-U12). All ours only (they call virtual slots or change
// objects; the state afterwards is checked):
// - CUnitServer::CheckOrder (0054C8B0): every queued order started (StartQueuedOrder; any move or
//   attack order). CUnitServer::CheckOrderFrom (0054C960): the AI's reach checks (a hostile unit
//   choosing a place to attack from); the unit's position must be what it was.
// - MotionSegmentInit (0054E5D0): TimeUntilAction (a unit's attack planned).
// - PathConstructSteps (0054EAC0): PlanOrderRoute and the AI's test paths (any move order).
// - ArmourSpell::Assign (00558840): RefreshItemSpells (a unit wearing an item with a spell).
// - CountedRefDelete / CallRecordDelete (00558CA0, 00558CC0): the brain's memory arrays
//   destroyed (a unit with a brain removed).
// WorldTick (0054B220) is four bytes: too short to hook.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/ai_motion.h"
#include "../../src/game/combat.h"
#include "../../src/game/unit.h"
#include "../../src/game/unit_ai.h"

#include <cstring>

void* __fastcall MotionSegmentInit(void* part);
AIPath* __fastcall PathConstructSteps(AIPath* path);
AIRef* __fastcall CountedRefDelete(AIRef* self, void*, unsigned flags);
AICallRecord* __fastcall CallRecordDelete(AICallRecord* self, void*, unsigned flags);

namespace {
void* s_unused;
} // namespace

static bool __fastcall CheckOrderVerify(CUnitServer* self, void*, const UnitAction* order) {
    const Vector3 before = self->unit->position;
    bool result = self->CheckOrder(order);
    ei::verify::Check("CUnitServer::CheckOrder", true, std::memcmp(&before, &self->unit->position, sizeof before) == 0,
                      "kind %d", order->kind);
    return result;
}
EI_VERIFY(0x0054C8B0, CheckOrderVerify, 0, s_unused);

static bool __fastcall CheckOrderFromVerify(CUnitServer* self, void*, const UnitAction* order, const Vector3* from) {
    const Vector3 before = self->unit->position;
    bool result = self->CheckOrderFrom(order, from);
    ei::verify::Check("CUnitServer::CheckOrderFrom", true,
                      std::memcmp(&before, &self->unit->position, sizeof before) == 0, "kind %d", order->kind);
    return result;
}
EI_VERIFY(0x0054C960, CheckOrderFromVerify, 0, s_unused);

static void* __fastcall MotionSegmentInitVerify(void* part) {
    void* result = MotionSegmentInit(part);
    // The part starts at UnitMotion's fromX; its segment is at +0x50.
    int segment;
    std::memcpy(&segment, static_cast<char*>(part) + 0x50, sizeof segment);
    ei::verify::Check("MotionSegmentInit", true, result == part && segment == -1, "");
    return result;
}
EI_VERIFY(0x0054E5D0, MotionSegmentInitVerify, 0, s_unused);

static AIPath* __fastcall PathConstructStepsVerify(AIPath* path) {
    AIPath* result = PathConstructSteps(path);
    ei::verify::Check("PathConstructSteps", true,
                      result == path && path->steps.count == 0 && !path->steps.head && !path->steps.tail &&
                          path->steps.blockSize == 0x80,
                      "");
    return result;
}
EI_VERIFY(0x0054EAC0, PathConstructStepsVerify, 0, s_unused);

static ArmourSpell* __fastcall ArmourSpellAssignVerify(ArmourSpell* self, void*, const ArmourSpell* other) {
    ArmourSpell* result = self->Assign(other);
    ei::verify::Check("ArmourSpell::Assign", true,
                      result == self && self->item == other->item && self->countdown == other->countdown &&
                          self->worn == other->worn,
                      "");
    return result;
}
EI_VERIFY(0x00558840, ArmourSpellAssignVerify, 0, s_unused);

static AIRef* __fastcall CountedRefDeleteVerify(AIRef* self, void*, unsigned flags) {
    AIRef* result = CountedRefDelete(self, nullptr, flags);
    // A freed block is not read.
    ei::verify::Check("CountedRefDelete", true, result == self && ((flags & 1) || !self->object), "");
    return result;
}
EI_VERIFY(0x00558CA0, CountedRefDeleteVerify, 0, s_unused);

static AICallRecord* __fastcall CallRecordDeleteVerify(AICallRecord* self, void*, unsigned flags) {
    AICallRecord* result = CallRecordDelete(self, nullptr, flags);
    ei::verify::Check("CallRecordDelete", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00558CC0, CallRecordDeleteVerify, 0, s_unused);
