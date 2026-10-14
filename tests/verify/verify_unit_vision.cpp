// Checks for unit_vision.cpp (A-U3).
// - WeaponPrototypeSkillBonus (00526860): pure; the original and ours on the same id, bitwise.
// - UnitParams::VisionRange (005271E0): pure; CheckNear (plain double maths against the x87's).
// - CUnit::AdvanceUnitTime (0052FC40, slot 25): ours only (it advances the controller and marks the
//   sight on the client world); runs.
// - CMapObject::SetVisibilityLevel (0050F170): ours only (slot 47 changes the companion); returns.
// Reached by: slot 25 for every unit as the client world's time moves on (every scenario with
// units); the skill bonus from the interface's weapon description (005F1590: ui_describe,
// ui_hero_stats); the visibility level from the client world's object updates (00565380 /
// 005652E0) and CPlayerClient::OnNewObject (every scenario that starts a game).
#include "verify.h"

#include "../../src/game/map_object.h"
#include "../../src/game/unit.h"

#include <cstdint>

double __stdcall WeaponPrototypeSkillBonus(std::uint32_t itemId);

namespace {
void* s_unusedTime;
void* s_unusedVisibility;
} // namespace

// 00526860: PUSH ESI; MOV ESI,[007C86FC] (7 bytes).
static double(__stdcall* s_skillBonus)(std::uint32_t);
static double __stdcall WeaponPrototypeSkillBonusVerify(std::uint32_t itemId) {
    double expected = s_skillBonus(itemId);
    double actual = WeaponPrototypeSkillBonus(itemId);
    ei::verify::Check("WeaponPrototypeSkillBonus", expected, actual, "item 0x%X", itemId);
    return expected;
}
EI_VERIFY(0x00526860, WeaponPrototypeSkillBonusVerify, 7, s_skillBonus);

// 005271E0: PUSH ECX; XOR EAX,EAX; XOR EDX,EDX (5 bytes).
static double(__fastcall* s_visionRange)(UnitParams*, void*, float);
static double __fastcall VisionRangeVerify(UnitParams* self, void*, float daylight) {
    double expected = s_visionRange(self, nullptr, daylight);
    double actual = self->VisionRange(daylight);
    ei::verify::CheckNear("UnitParams::VisionRange", static_cast<float>(expected), static_cast<float>(actual),
                          "values %d / %d, daylight %g", self->valuesB0[0], self->valuesB0[1], daylight);
    return expected;
}
EI_VERIFY(0x005271E0, VisionRangeVerify, 5, s_visionRange);

static void __fastcall AdvanceUnitTimeVerify(CUnit* self, void*, float time) {
    self->CUnit::AdvanceTime(time);
    ei::verify::Check("CUnit::AdvanceUnitTime", true, true, "unit %d", self->id_);
}
EI_VERIFY(0x0052FC40, AdvanceUnitTimeVerify, 0, s_unusedTime);

static int __fastcall SetVisibilityLevelVerify(CMapObject* self, void*, float level) {
    int result = self->SetVisibilityLevel(level);
    ei::verify::Check("CMapObject::SetVisibilityLevel", true, true, "object %d, level %g", self->id_, level);
    return result;
}
EI_VERIFY(0x0050F170, SetVisibilityLevelVerify, 0, s_unusedVisibility);
