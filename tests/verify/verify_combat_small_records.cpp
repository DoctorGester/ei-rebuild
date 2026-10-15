// Checks for combat_small_records.cpp (A-U12). All ours only (they change objects; the state
// afterwards is checked):
// - MotionSegmentInit (0054E5D0): TimeUntilAction (a unit's attack planned).
// - ArmourSpell::Assign (00558840): RefreshItemSpells (a unit wearing an item with a spell).
// WorldTick (0054B220) is four bytes: too short to hook.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/ai_motion.h"
#include "../../src/game/combat.h"
#include "../../src/game/unit_ai.h"

#include <cstring>

void* __fastcall MotionSegmentInit(void* part);

namespace {
void* s_unused;
} // namespace

static void* __fastcall MotionSegmentInitVerify(void* part) {
    void* result = MotionSegmentInit(part);
    // The part starts at UnitMotion's fromX; its segment is at +0x50.
    int segment;
    std::memcpy(&segment, static_cast<char*>(part) + 0x50, sizeof segment);
    ei::verify::Check("MotionSegmentInit", true, result == part && segment == -1, "");
    return result;
}
EI_VERIFY(0x0054E5D0, MotionSegmentInitVerify, 0, s_unused);

static ArmourSpell* __fastcall ArmourSpellAssignVerify(ArmourSpell* self, void*, const ArmourSpell* other) {
    ArmourSpell* result = self->Assign(other);
    ei::verify::Check("ArmourSpell::Assign", true,
                      result == self && self->item == other->item && self->countdown == other->countdown &&
                          self->worn == other->worn,
                      "");
    return result;
}
EI_VERIFY(0x00558840, ArmourSpellAssignVerify, 0, s_unused);
