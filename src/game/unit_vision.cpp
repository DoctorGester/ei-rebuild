// A unit's sight on the client (its range by daylight, marked on the client world as time moves on), a
// weapon prototype's skill bonus and the map object's visibility level.
#include "unit.h"

#include "item_db.h"
#include "map_object.h"
#include "object_tables.h"
#include "world.h"
#include "game_singletons.h"

#include "lib/nan_audit.h"

#include <cstdint>

namespace {

constexpr float kPercent = 0.01f;
constexpr float kNoSkill = 0.0f;

} // namespace

// The skill bonus byte of the weapon prototype `itemId >> 16`: skillBonus[0] for weapon types 0-4 and 8,
// skillBonus[1] for 5 and 6; unrounded. An unknown prototype reads its type from address 0x0C (it faults),
// other types index 0x7FFFFFFF bytes on (kept).
double WeaponPrototypeSkillBonus(std::uint32_t itemId) {
    const auto* weapon =
        itemdb::WeaponAt(itemId >> 16);
    int type = weapon ? weapon->type : *reinterpret_cast<const volatile int*>(std::uintptr_t{0x0C});   // faults like the game
    int skill = 0x7FFFFFFF;
    if (type < 5 || type == 8) skill = 0;
    else if (type == 5 || type == 6) skill = 1;
    if (!weapon) return kNoSkill;
    return static_cast<double>(weapon->skillBonus[skill]);
}

double UnitParams::VisionRange(float daylight) {
    double day = static_cast<double>(perception[1]) * daylight * kPercent;
    return (day + (1.0 - daylight)) * perception[0];
}

void CUnit::AdvanceTime(float time) {
    if (controller) Controller()->AdvanceTime(time);
    if (!(IsTextKey(id_) & 0xFF)) return;
    float daylight = static_cast<float>(g_clientWorld->NightLevel());
    float range = static_cast<float>(params.VisionRange(daylight));
    if (moveMode == 0) range = range;
    g_clientWorld->MarkFootprintsInSight(&localPosition_.x, range);
}

int CMapObject::SetVisibilityLevel(float level) {
    CMapObject* slots = this;
    EI_NAN_AUDIT(level);
    if (level == 0.0f) return slots->SetVisibilityMode(0);
    if (level == 1.0f) return slots->SetVisibilityMode(1);
    return slots->SetVisibilityMode(2);
}
