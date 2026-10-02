// Differential checks for the AI map passability queries (no side effects).
#include "verify.h"

#include "../../src/game/ai_map.h"

// 005B80F0: PUSH EBP; MOV EBP,ESP; PUSH EBX; PUSH ESI (5 bytes).
// 005B79D0: PUSH EBX; PUSH EBP; PUSH ESI; PUSH EDI; MOV EDI,[ESP+0x14] (8 bytes).
static bool(__fastcall* s_canStandAt)(CAIMap*, void*, CUnit*, int, int, int*);
static bool(__fastcall* s_isPassable)(CAIMap*, void*, CUnit*, int, int);

static bool __fastcall CanStandAtVerify(CAIMap* self, void*, CUnit* unit, int x, int y, int* reason) {
    int expectedReason = -1, actualReason = -1;
    bool expected = s_canStandAt(self, nullptr, unit, x, y, &expectedReason);
    bool actual = self->CanStandAt(unit, x, y, &actualReason);
    ei::verify::Check("CAIMap::CanStandAt", true, expected == actual && expectedReason == actualReason,
                      "(%d,%d) %d/%d vs %d/%d", x, y, expected, expectedReason, actual, actualReason);
    if (expectedReason != -1) *reason = expectedReason;
    return expected;
}
EI_VERIFY(0x005B80F0, CanStandAtVerify, 5, s_canStandAt);

static bool __fastcall IsPassableVerify(CAIMap* self, void*, CUnit* unit, int x, int y) {
    bool expected = s_isPassable(self, nullptr, unit, x, y);
    ei::verify::Check("CAIMap::IsPassable", expected, self->IsPassable(unit, x, y), "(%d,%d)", x, y);
    return expected;
}
EI_VERIFY(0x005B79D0, IsPassableVerify, 8, s_isPassable);

// 005BC3E0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x34 (6 bytes). No side effects.
static float(__fastcall* s_lineOfSight)(CAIMap*, void*, const Vector3*, const Vector3*);

static float __fastcall LineOfSightVerify(CAIMap* self, void*, const Vector3* from, const Vector3* to) {
    float expected = s_lineOfSight(self, nullptr, from, to);
    ei::verify::Check("CAIMap::LineOfSight", expected, self->LineOfSight(from, to), "(%g %g %g)->(%g %g %g)", from->x,
                      from->y, from->z, to->x, to->y, to->z);
    return expected;
}
EI_VERIFY(0x005BC3E0, LineOfSightVerify, 6, s_lineOfSight);
