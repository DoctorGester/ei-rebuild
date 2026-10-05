// Checks for unit_skill_price.cpp: UnitStats::SkillPrice (00528050) is pure; the original and ours
// run side by side and the prices must match exactly. Plain C++ maths (rule 7): a level's worth
// is truncated, so a worth difference that lands within a few ULPs of a whole number may come out
// one apart (rare; reported, not hidden). Reached from the interface's skill pages (005F1590,
// 00625880); the verify count shows whether a scenario opens one.
#include "verify.h"

#include "../../src/game/unit.h"

// 00528050: PUSH EBP; MOV EBP,ESP; AND ESP,0xFFFFFFF8 (6 bytes).
static int(__fastcall* s_skillPrice)(UnitStats*, void*, const char*);
static int __fastcall SkillPriceVerify(UnitStats* self, void*, const char* record) {
    int expected = s_skillPrice(self, nullptr, record);
    int actual = self->SkillPrice(record);
    ei::verify::Check("UnitStats::SkillPrice", expected, actual, "record %p: %d vs %d",
                      static_cast<const void*>(record), expected, actual);
    return expected;
}
EI_VERIFY(0x00528050, SkillPriceVerify, 6, s_skillPrice);
