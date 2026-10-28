// Creature statistics: attribute, skill and skill level lookups over UnitStats, and small helpers
// of the hit (DamageInfo) and lock records. See unit.h.
#include "logic_settings.h"
#include "unit.h"
#include "unit_db.h"

#include "spell.h"

#include "ei/rebirth.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstring>

namespace {
constexpr float kStatBase = 25.0f;
constexpr float kTwelve = 12.0f;
constexpr float kPercent = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(kPercent) == 0x3C23D70Au);
constexpr float kFactorBase = 15.0f;
constexpr float kPointThree = 0.3f;
static_assert(std::bit_cast<std::uint32_t>(kPointThree) == 0x3E99999Au);
constexpr float kPointSeven = 0.7f;
static_assert(std::bit_cast<std::uint32_t>(kPointSeven) == 0x3F333333u);
constexpr float kPointTwo = 0.2f;
static_assert(std::bit_cast<std::uint32_t>(kPointTwo) == 0x3E4CCCCDu);
constexpr int kDamageWords = 7;   // words 1..7 of a damage / protection block
constexpr float kPointEight = 0.8f;
static_assert(std::bit_cast<std::uint32_t>(kPointEight) == 0x3F4CCCCDu);

// Skill levels 21 and 22 index the level table from these bases.
constexpr int kScaledStrengthSkill = 21;   // skillLevels[21] and [22]: the two skills these read
constexpr int kBackstabSkill = 22;
constexpr int kScaledStrengthLevelBase = 0x3E;
constexpr int kBackstabLevelBase = 0x41;

// The stat range GetFactors divides by (a static initialiser: 35 - 15).
constexpr float kFactorStatRange = /*007AF280*/ 20.0f;

// The skill level record `index`, or null when there is none.
const SkillLevelRecord* LevelRecord(std::uint32_t index) {
    return unitdb::SkillLevelAt(index);
}

} // namespace

double UnitStats::StrengthRatio() { return strength / g_logicSettings.hpVal[0]; }

double UnitStats::DexterityRatio() { return dexterity / g_logicSettings.mpVal[0]; }

double UnitStats::Period20() { return kStatBase / intelligence; }

float UnitStats::AttributeBase(int index) { return static_cast<float>(attributes[index]); }

double UnitStats::AttributeWithBonus(int index) {
    switch (index) {
    case 0: return (dexterity - kStatBase) + static_cast<float>(attributes[0]);
    case 1: return (dexterity - kStatBase) + static_cast<float>(attributes[1]);
    case 2: return (intelligence - kStatBase) + static_cast<float>(attributes[2]);
    default: return 0.0f;
    }
}

float UnitStats::SkillBase(int index) { return static_cast<float>(skills[index]); }

double UnitStats::SkillWithBonus(std::uint32_t index) {
    float skill = static_cast<float>(skills[index]);
    // The first letter of the skill's first stat name picks the stat; a missing record faults.
    const SkillRecord* skillRecord = unitdb::SkillAt(index);
    switch (skillRecord->statNames.begin()[0][0]) {
    case 'd': return (dexterity - kStatBase) + skill;
    case 'i': return (intelligence - kStatBase) + skill;
    case 's': return (strength - kStatBase) + skill;
    default: return skill;
    }
}

float UnitStats::WeaponSkill(int type) {
    if (type < 5) return static_cast<float>(skills[0]);
    if (type == 5 || type == 6) return static_cast<float>(skills[1]);
    if (type == 8) return static_cast<float>(skills[0]);
    return 0.0f;
}

::SkillLevelRecord* UnitStats::SkillLevelRecord(int index) {
    std::uint8_t level = skillLevels[index];
    if (level == 0) return nullptr;
    return unitdb::SkillLevelAt(static_cast<std::uint32_t>(index * 3 - 1 + level));
}

double UnitStats::SkillLevelValue(int index) {
    std::uint8_t level = skillLevels[index];
    if (level != 0) {
        const ::SkillLevelRecord* record = LevelRecord(static_cast<std::uint32_t>(index * 3 - 1 + level));
        if (record) return record->Value();
    }
    return 0.0f;
}

double UnitStats::ScaledStrength() {
    double scaledStrength = strength * kTwelve;   // kept on the FPU stack
    std::uint8_t level = skillLevels[kScaledStrengthSkill];
    if (level != 0) {
        const ::SkillLevelRecord* record = LevelRecord(level + kScaledStrengthLevelBase);
        if (record) {
            return (record->Value() * kPercent + 1.0f) * scaledStrength;
        }
    }
    return (0.0f * kPercent + 1.0f) * scaledStrength;
}

double UnitStats::BackstabFactor() {
    double base = g_logicSettings.backstabAdd;
    std::uint8_t level = skillLevels[kBackstabSkill];
    if (level != 0) {
        const ::SkillLevelRecord* record = LevelRecord(level + kBackstabLevelBase);
        if (record) {
            return record->Value() * kPercent + base;
        }
    }
    return 0.0f * kPercent + base;
}

double UnitStats::Reduce(float value) {
    double taken = 0.0;   // accumulated on the FPU stack
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind < 1) continue;
        if (slot.current >= 0.0f) {
            taken = (slot.maximum - slot.current) / slot.maximum * slot.factor * value + taken;
        } else {
            taken = value * slot.factor + taken;   // also NaN
        }
    }
    return value - taken;   // unrounded
}

double UnitStats::ReduceKind1(float value) {
    double taken = 0.0;
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind == 1) taken = value * slot.factor + taken;
    }
    return value - taken;   // unrounded
}

void UnitStats::GetFactors(float* a, float* b, int) {
    double x = (strength - kFactorBase) / kFactorStatRange;   // both kept on the FPU stack
    double y = 1.0f - (dexterity - kFactorBase) / kFactorStatRange;
    *a = static_cast<float>(y * kPointThree + x * kPointSeven);
    *b = static_cast<float>((y * kPointEight + x * kPointTwo) * kPointEight + kPointTwo);
}

int LockKey::Opens(const LockKey* lock) const {
    std::uint32_t common = lock->mask & mask;
    if (common == 0) return 0;
    if ((common & 8) == 8 && id == lock->id) return 1;
    if ((common & 7) == 0) return 0;
    if (ei::rebirth::Enabled(ei::rebirth::Patch::FixLeversActivate)) {
        // Rebirth: levels compared signed; a lock of level 0 gives the key's level + 1, so a key of level -1
        // (0xFFFFFFFF) no longer opens it.
        if (lock->level == 0) return static_cast<int>(level + 1);
        return static_cast<std::int32_t>(level) < static_cast<std::int32_t>(lock->level) ? 0 : 1;
    }
    return level >= lock->level ? 1 : 0;
}

std::uint32_t ResetDamageInfo(DamageInfo* info) {
    constexpr std::uint32_t kLarge = 0x7C23D70A;
    std::memset(info, 0, 0x3C);
    for (float& weight : info->typeWeights) weight = 1.0f;
    info->multiplier = 1.0f;
    info->weaponValue = 0;
    info->attackRoll = std::bit_cast<float>(kLarge);
    info->amount = std::bit_cast<float>(kLarge);
    info->location = 6;
    info->hitKind = 0;
    return kLarge;
}

BOOL HasElementalDamage(const DamageInfo* info) {
    EI_NAN_AUDIT(info->typeWeights[3], info->typeWeights[4], info->typeWeights[5]);
    if (info->typeWeights[3] == 0.0f && info->typeWeights[4] == 0.0f && info->typeWeights[5] == 0.0f) {
        return 0;
    }
    return 1;
}

std::uint32_t DamageWords::ExcessOver(const DamageWords* protection) const {
    std::uint16_t sum = 0;
    for (int i = 1; i <= kDamageWords; ++i) {
        const std::uint16_t difference = static_cast<std::uint16_t>(words[i] - protection->words[i]);
        if (!(difference & 0x8000)) sum = static_cast<std::uint16_t>(sum + difference);
    }
    return sum;
}

// The largest of the damage words 1..7 (unsigned).
std::uint16_t MaxDamage(const std::uint16_t* damage) {
    std::uint16_t largest = 0;
    for (int i = 1; i <= kDamageWords; ++i)
        if (largest <= damage[i]) largest = damage[i];
    return largest;
}
