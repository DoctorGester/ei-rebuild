// The weapon skill bonus on a unit's packed parameters (00527850). See unit.h (UnitParams) and
// docs/notes/units.md ("Weapon skill on the packed parameters").
#include "item_db.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cstdint>

namespace {
constexpr double kStatBase = 25.0;   // 0073E124
constexpr int kWeaponSkill0Bonus = 0;   // WeaponRecord::skillBonus (+0x7B / +0x7C)
constexpr int kWeaponSkill1Bonus = 1;

// The level value (+0x34, unsigned) of the skill level record of skill `type` at the packed level
// byte, or null without one. The byte is read unchecked (`type` may be negative), as the original.
const SkillLevelRecord* LevelRecord(const UnitParams* params, int type) {
    std::uint8_t level = params->skillLevels[type];
    if (level == 0) return nullptr;
    return reinterpret_cast<const SkillLevelRecord*>(
        unitdb::SkillLevels().At(static_cast<std::uint32_t>(type * 3 - 1 + level), unitdb::kSkillLevelSize));
}

// The first letter of skill `skill`'s name (+0x0C leads to it): which stat counts ('s', 'd',
// 'i'). The record is read unchecked (an empty skill table faults as in the original).
char StatLetter(int skill) {
    auto* record = reinterpret_cast<const SkillRecord*>(unitdb::Skills().At(static_cast<std::uint32_t>(skill), unitdb::kSkillSize));
    const char* name = record->statNames.begin()[0];
    return name[0];
}

// The stat that letter names, minus 25, or false for another letter.
bool StatTerm(const UnitParams* params, char letter, double* term) {
    switch (letter) {
    case 's': *term = static_cast<double>(params->stat10) - kStatBase; return true;
    case 'i': *term = static_cast<double>(params->attackRate) - kStatBase; return true;
    case 'd': *term = static_cast<double>(params->stat18) - kStatBase; return true;
    default: return false;
    }
}

double WeaponByte(const char* weapon, int skill) {
    return weapon ? static_cast<double>(reinterpret_cast<const itemdb::WeaponRecord*>(weapon)->skillBonus[skill]) : 0.0;
}
} // namespace

double UnitParams::WeaponSkillBonus(int type, const char* weapon) {
    double term;
    if (type < 5) {
        // The level value stays on the FPU (FILD qword); the sums are taken as the original's.
        const SkillLevelRecord* record = LevelRecord(this, type);
        double level = record ? record->Value() : 0.0;
        double bonus = WeaponByte(weapon, kWeaponSkill0Bonus);
        double skill = skills[0];
        if (StatTerm(this, StatLetter(0), &term)) return level + ((term + skill) + bonus);
        return level + (skill + bonus);
    }
    if (type == 5 || type == 6) {
        // Here the level value is stored to a float first (FSTP float).
        const SkillLevelRecord* record = LevelRecord(this, type);
        float level = record ? static_cast<float>(record->Value()) : 0.0f;
        double bonus = WeaponByte(weapon, kWeaponSkill1Bonus);
        double skill = skills[1];
        if (StatTerm(this, StatLetter(1), &term)) return ((term + skill) + bonus) + level;
        return (skill + bonus) + level;
    }
    if (type == 8) {
        double bonus = WeaponByte(weapon, kWeaponSkill0Bonus);
        double skill = skills[0];
        if (StatTerm(this, StatLetter(0), &term)) return (term + skill) + bonus;
        return skill + bonus;
    }
    return 0.0;
}
EI_REPLACE_METHOD(0x00527850, UnitParams::WeaponSkillBonus);
