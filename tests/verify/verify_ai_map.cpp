// Differential checks for the AI map queries (no side effects unless noted).
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstring>

namespace {
void* s_unused;

template <typename T>
bool SameBits(const T& a, const T& b) { return std::memcmp(&a, &b, sizeof(T)) == 0; }
} // namespace

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

// 005B81B0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x10 (6 bytes). Writes only *reason.
static bool(__fastcall* s_canStandFromHere)(CAIMap*, void*, CUnit*, int, int, int*);

static bool __fastcall CanStandAtFromHereVerify(CAIMap* self, void*, CUnit* unit, int x, int y, int* reason) {
    int expectedReason = -1, actualReason = -1;
    bool expected = s_canStandFromHere(self, nullptr, unit, x, y, &expectedReason);
    bool actual = self->CanStandAtFromHere(unit, x, y, &actualReason);
    ei::verify::Check("CAIMap::CanStandAtFromHere", true, expected == actual && expectedReason == actualReason,
                      "(%d,%d) %d/%d vs %d/%d", x, y, expected, expectedReason, actual, actualReason);
    if (expectedReason != -1) *reason = expectedReason;
    return expected;
}
EI_VERIFY(0x005B81B0, CanStandAtFromHereVerify, 6, s_canStandFromHere);

static bool __fastcall IsPassableVerify(CAIMap* self, void*, CUnit* unit, int x, int y) {
    bool expected = s_isPassable(self, nullptr, unit, x, y);
    ei::verify::Check("CAIMap::IsPassable", expected, self->IsPassable(unit, x, y), "(%d,%d)", x, y);
    return expected;
}
EI_VERIFY(0x005B79D0, IsPassableVerify, 8, s_isPassable);

// 005B7A40 takes the unit out of the sector map and puts it back, so it runs once (ours). A unit's
// footprint lives in the unit layers, not in the cells, so the answer must equal IsPassable at the
// target cell afterwards.
static bool __fastcall IsPassableWithoutSelfVerify(CAIMap* self, void*, CUnit* unit, const Vector3* position) {
    Vector3 p = *position;
    bool actual = self->IsPassableWithoutSelf(unit, position);
    bool expected = self->IsPassable(unit, WorldToCell(p.x), WorldToCell(p.y));
    ei::verify::Check("CAIMap::IsPassableWithoutSelf", expected, actual, "(%g,%g)", p.x, p.y);
    return actual;
}
EI_VERIFY(0x005B7A40, IsPassableWithoutSelfVerify, 0, s_unused);

// Position queries. All start PUSH EBP; MOV EBP,ESP; SUB ESP,n (6 bytes) unless noted.
#define POSITION_QUERY(addr, R, Method)                                                                  \
    static R(__fastcall* s_##Method)(CAIMap*, void*, const Vector3*);                                    \
    static R __fastcall Method##Verify(CAIMap* self, void*, const Vector3* p) {                         \
        R expected = s_##Method(self, nullptr, p);                                                       \
        ei::verify::Check("CAIMap::" #Method, expected, self->Method(p), "(%g,%g)", p->x, p->y);        \
        return expected;                                                                                 \
    }                                                                                                    \
    EI_VERIFY(addr, Method##Verify, 6, s_##Method);

POSITION_QUERY(0x005BB960, float, GroundHeightAt)
POSITION_QUERY(0x005BBA10, float, SurfaceHeightAt)
POSITION_QUERY(0x005BBB20, float, TopHeightAt)
POSITION_QUERY(0x005BC2A0, float, SlopeAt)
POSITION_QUERY(0x005BBBD0, bool, IsBridgeAt)
POSITION_QUERY(0x005BBC70, bool, CellFlag80At)
POSITION_QUERY(0x005BBDB0, bool, HasKind3OccluderAt)
POSITION_QUERY(0x005BBE40, int, MaterialAt)
POSITION_QUERY(0x005BC1F0, void*, MaterialRecordAt)

// 005B77F0: MOV EDX,[ECX+0x269C] (6 bytes).
static float(__fastcall* s_groundHeight)(CAIMap*, void*, int, int);
static float __fastcall GroundHeightVerify(CAIMap* self, void*, int x, int y) {
    float expected = s_groundHeight(self, nullptr, x, y);
    ei::verify::Check("CAIMap::GroundHeight", expected, self->GroundHeight(x, y), "(%d,%d)", x, y);
    return expected;
}
EI_VERIFY(0x005B77F0, GroundHeightVerify, 6, s_groundHeight);

// 005BBD10: MOV EDX,[ECX+0x26AC] (6 bytes).
static bool(__fastcall* s_hasKind3)(CAIMap*, void*, int, int);
static bool __fastcall HasKind3OccluderVerify(CAIMap* self, void*, int x, int y) {
    bool expected = s_hasKind3(self, nullptr, x, y);
    ei::verify::Check("CAIMap::HasKind3Occluder", expected, self->HasKind3Occluder(x, y), "(%d,%d)", x, y);
    return expected;
}
EI_VERIFY(0x005BBD10, HasKind3OccluderVerify, 6, s_hasKind3);

// 005B64E0: PUSH ECX; MOV EAX,[ESP+0xC] (5 bytes). Writes only *height.
static bool(__fastcall* s_bridgeDeck)(CAIMap*, void*, int, int, int*);
static bool __fastcall BridgeDeckAtVerify(CAIMap* self, void*, int x, int y, int* height) {
    int expectedHeight = -1, actualHeight = -1;
    bool expected = s_bridgeDeck(self, nullptr, x, y, height ? &expectedHeight : nullptr);
    bool actual = self->BridgeDeckAt(x, y, height ? &actualHeight : nullptr);
    ei::verify::Check("CAIMap::BridgeDeckAt", true, expected == actual && expectedHeight == actualHeight,
                      "(%d,%d) %d/%d vs %d/%d", x, y, expected, expectedHeight, actual, actualHeight);
    if (height) *height = expectedHeight;
    return expected;
}
EI_VERIFY(0x005B64E0, BridgeDeckAtVerify, 5, s_bridgeDeck);

// 005BA6E0 / 005BA790 / 005BA830: PUSH EBP; MOV EBP,ESP; SUB ESP,n (6 bytes).
static bool(__fastcall* s_unitCellPassable)(CAIMap*, void*, CUnit*);
static bool __fastcall IsUnitCellPassableVerify(CAIMap* self, void*, CUnit* unit) {
    bool expected = s_unitCellPassable(self, nullptr, unit);
    ei::verify::Check("CAIMap::IsUnitCellPassable", expected, self->IsUnitCellPassable(unit), "(%g,%g)",
                      unit->position.x, unit->position.y);
    return expected;
}
EI_VERIFY(0x005BA6E0, IsUnitCellPassableVerify, 6, s_unitCellPassable);

static bool(__fastcall* s_passableForClass)(CAIMap*, void*, std::uint8_t, const Vector3*);
static bool __fastcall IsPassableForClassVerify(CAIMap* self, void*, std::uint8_t sizeClass, const Vector3* p) {
    bool expected = s_passableForClass(self, nullptr, sizeClass, p);
    ei::verify::Check("CAIMap::IsPassableForClass", expected, self->IsPassableForClass(sizeClass, p),
                      "class %d (%g,%g)", sizeClass, p->x, p->y);
    return expected;
}
EI_VERIFY(0x005BA790, IsPassableForClassVerify, 6, s_passableForClass);

static bool(__fastcall* s_canLeave)(CAIMap*, void*, CUnit*);
static bool __fastcall CanLeaveCellVerify(CAIMap* self, void*, CUnit* unit) {
    bool expected = s_canLeave(self, nullptr, unit);
    ei::verify::Check("CAIMap::CanLeaveCell", expected, self->CanLeaveCell(unit), "(%g,%g)", unit->position.x,
                      unit->position.y);
    return expected;
}
EI_VERIFY(0x005BA830, CanLeaveCellVerify, 6, s_canLeave);

// 005B7B20 / 005B7B70: PUSH EBP; MOV EBP,ESP; PUSH ECX; MOV EAX,[EBP+8] (7 bytes).
static int(__stdcall* s_levelForClass)(int);
static int __stdcall LevelForSizeClassVerify(int sizeClass) {
    int expected = s_levelForClass(sizeClass);
    ei::verify::Check("LevelForSizeClass", expected, LevelForSizeClass(sizeClass), "class %d", sizeClass);
    return expected;
}
EI_VERIFY(0x005B7B20, LevelForSizeClassVerify, 7, s_levelForClass);

static int(__stdcall* s_standingLevel)(const CUnit*);
static int __stdcall StandingLevelVerify(const CUnit* unit) {
    int expected = s_standingLevel(unit);
    ei::verify::Check("StandingLevel", expected, StandingLevel(unit), "height %g", unit->standingHeight);
    return expected;
}
EI_VERIFY(0x005B7B70, StandingLevelVerify, 7, s_standingLevel);

// 005B3160: MOVSX EAX,word ptr [ESP+8] (5 bytes).
static int(__fastcall* s_bridgeHeight)(const void*, void*, short, short);
static int __fastcall BridgeHeightVerify(const void* map, void*, short x, short y) {
    int expected = s_bridgeHeight(map, nullptr, x, y);
    ei::verify::Check("BridgeHeight", expected, BridgeHeight(map, nullptr, x, y), "(%d,%d)", x, y);
    return expected;
}
EI_VERIFY(0x005B3160, BridgeHeightVerify, 5, s_bridgeHeight);

// 005BC3E0: PUSH EBP; MOV EBP,ESP; SUB ESP,0x34 (6 bytes). No side effects.
static float(__fastcall* s_lineOfSight)(CAIMap*, void*, const Vector3*, const Vector3*);

static float __fastcall LineOfSightVerify(CAIMap* self, void*, const Vector3* from, const Vector3* to) {
    float expected = s_lineOfSight(self, nullptr, from, to);
    ei::verify::Check("CAIMap::LineOfSight", expected, self->LineOfSight(from, to), "(%g %g %g)->(%g %g %g)", from->x,
                      from->y, from->z, to->x, to->y, to->z);
    return expected;
}
EI_VERIFY(0x005BC3E0, LineOfSightVerify, 6, s_lineOfSight);

// 005BFC40 updates its cache: run the original on a copy of the cache. PUSH ECX; FLD [ESP+8] (5 bytes).
static const std::int16_t*(__fastcall* s_spiralGet)(SpiralCache*, void*, float, int*);
static const std::int16_t* __fastcall SpiralGetVerify(SpiralCache* self, void*, float radius, int* length) {
    SpiralCache scratch = *self;
    int expectedLength = -1, actualLength = -1;
    const std::int16_t* expected = s_spiralGet(&scratch, nullptr, radius, &expectedLength);
    const std::int16_t* actual = self->Get(radius, &actualLength);
    bool same = expected == actual && expectedLength == actualLength && SameBits(scratch, *self);
    ei::verify::Check("SpiralCache::Get", true, same, "r %g: %d vs %d", radius, expectedLength, actualLength);
    *length = actualLength;
    return actual;
}
EI_VERIFY(0x005BFC40, SpiralGetVerify, 5, s_spiralGet);

// 005BA9D0 only writes `target` (and initialises its spiral cache, which the original call does
// first): run the original into a copy of the target. PUSH EBP; MOV EBP,ESP; SUB ESP,0x24 (6 bytes).
static bool(__fastcall* s_findFree)(CAIMap*, void*, CUnit*, const Vector3*, Vector3*);
static bool __fastcall FindFreeCellNearVerify(CAIMap* self, void*, CUnit* unit, const Vector3* from, Vector3* target) {
    Vector3 scratch = *target;
    Vector3 start = *from;
    bool expected = s_findFree(self, nullptr, unit, &start, &scratch);
    bool actual = self->FindFreeCellNear(unit, &start, target);
    ei::verify::Check("CAIMap::FindFreeCellNear", true, expected == actual && SameBits(scratch, *target),
                      "%d (%g,%g) vs %d (%g,%g)", expected, scratch.x, scratch.y, actual, target->x, target->y);
    return actual;
}
EI_VERIFY(0x005BA9D0, FindFreeCellNearVerify, 6, s_findFree);
