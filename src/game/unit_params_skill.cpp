// The weapon skill bonus on a unit's packed parameters. See unit.h (UnitParams) and
// docs/notes/units.md ("Statistics in play").
#include "item_db.h"
#include "unit.h"
#include "unit_db.h"

#include <cstdint>

namespace {
constexpr float kStatBase = 25.0f;
constexpr int kWeaponSkill0Bonus = 0;   // WeaponRecord::skillBonus
constexpr int kWeaponSkill1Bonus = 1;

// The level value (unsigned) of the skill level record of skill `type` at the packed level byte, or null
// without one. The byte is read unchecked (`type` may be negative).
const SkillLevelRecord* LevelRecord(const UnitParams* params, int type) {
    std::uint8_t level = params->skillLevels[type];
    if (level == 0) return nullptr;
    return unitdb::SkillLevelAt(static_cast<std::uint32_t>(type * 3 - 1 + level));
}

// The first letter of skill `skill`'s name: which stat counts ('s', 'd', 'i'). The record is read unchecked
// (an empty skill table faults).
char StatLetter(int skill) {
    const SkillRecord* record = unitdb::SkillAt(static_cast<std::uint32_t>(skill));
    return record->statNames.begin()[0][0];
}

// The stat that letter names, minus 25, or false for another letter.
bool StatTerm(const UnitParams* params, char letter, float* term) {
    switch (letter) {
    case 's': *term = params->strength - kStatBase; return true;
    case 'i': *term = params->intelligence - kStatBase; return true;
    case 'd': *term = params->dexterity - kStatBase; return true;
    default: return false;
    }
}

float WeaponByte(const itemdb::WeaponRecord* weapon, int skill) {
    return weapon ? static_cast<float>(weapon->skillBonus[skill]) : 0.0f;
}
} // namespace

float UnitParams::WeaponSkillBonus(int type, const itemdb::WeaponRecord* weapon) {
    float term;
    if (type < 5) {
        // The sums in the original's order.
        const SkillLevelRecord* record = LevelRecord(this, type);
        float level = record ? record->Value() : 0.0f;
        float bonus = WeaponByte(weapon, kWeaponSkill0Bonus);
        float skill = skills[0];
        if (StatTerm(this, StatLetter(0), &term)) return level + ((term + skill) + bonus);
        return level + (skill + bonus);
    }
    if (type == 5 || type == 6) {
        const SkillLevelRecord* record = LevelRecord(this, type);
        float level = record ? record->Value() : 0.0f;
        float bonus = WeaponByte(weapon, kWeaponSkill1Bonus);
        float skill = skills[1];
        if (StatTerm(this, StatLetter(1), &term)) return ((term + skill) + bonus) + level;
        return (skill + bonus) + level;
    }
    if (type == 8) {
        float bonus = WeaponByte(weapon, kWeaponSkill0Bonus);
        float skill = skills[0];
        if (StatTerm(this, StatLetter(0), &term)) return (term + skill) + bonus;
        return skill + bonus;
    }
    return 0.0f;
}

