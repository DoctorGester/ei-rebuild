// A unit's sight on the client (its range by daylight, marked on the client world as time moves
// on), a weapon prototype's skill bonus and the map object's visibility level (A-U3).
#include "unit.h"

#include "item_db.h"
#include "map_object.h"
#include "object_tables.h"
#include "originals_units.h"
#include "originals_world.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstdint>

namespace {

constexpr float kPercent = 0.01f;       // 0073D09C
constexpr float kNoSkill = 0.0f;        // 0073B858
constexpr float kOne = 1.0f;            // 0073B84C, 0073E9C4

void* ClientWorldObject() { return reinterpret_cast<void*>(0x007AF5B8); }

} // namespace

// 00526860 (__stdcall, RET 4). The skill bonus byte of the weapon prototype `itemId >> 16`: +0x7B
// for weapon types 0-4 and 8, +0x7C for 5 and 6; ST0. An unknown prototype reads its type from
// address 0x0C (the original faults), other types index 0x7FFFFFFF bytes on (kept).
double __stdcall WeaponPrototypeSkillBonus(std::uint32_t itemId) {
    const auto* weapon =
        reinterpret_cast<const itemdb::WeaponRecord*>(itemdb::Weapons().At(itemId >> 16, itemdb::kWeaponSize));
    int type = weapon ? weapon->type : *reinterpret_cast<const volatile int*>(std::uintptr_t{0x0C});   // faults like the original
    int skill = 0x7FFFFFFF;
    if (type < 5 || type == 8) skill = 0;
    else if (type == 5 || type == 6) skill = 1;
    if (!weapon) return kNoSkill;
    return static_cast<double>(weapon->skillBonus[skill]);
}
EI_REPLACE(0x00526860, WeaponPrototypeSkillBonus);

double UnitParams::VisionRange(float daylight) {
    double day = static_cast<double>(valuesB0[1]) * daylight * kPercent;
    return (day + (static_cast<double>(kOne) - daylight)) * valuesB0[0];
}
EI_REPLACE_METHOD(0x005271E0, UnitParams::VisionRange);

void CUnit::AdvanceUnitTime(float time) {
    if (controller) Controller()->AdvanceTime(time);
    if (!(IsTextKey(id_) & 0xFF)) return;
    volatile float daylight = static_cast<float>(WorldDayTimeOriginal(ClientWorldObject()));
    volatile float range = static_cast<float>(UnitParamsVisionRangeOriginal(Params(), nullptr, daylight));
    if (moveMode == 0) range = range * kOne;
    ClientWorldMarkSightOriginal(ClientWorldObject(), nullptr, &positionX, range);
}
EI_REPLACE_METHOD(0x0052FC40, CUnit::AdvanceUnitTime);

int CMapObject::SetVisibilityLevel(float level) {
    CMapObjectVirtuals* slots = ObjectSlots(this);
    // FCOMP; TEST AH,0x40: C3, set for equal and for NaN.
    if (x87::IsNaN(level) || level == 0.0f) return slots->SetVisibilityMode(0);
    if (level == kOne) return slots->SetVisibilityMode(1);
    return slots->SetVisibilityMode(2);
}
EI_REPLACE_METHOD(0x0050F170, CMapObject::SetVisibilityLevel);
