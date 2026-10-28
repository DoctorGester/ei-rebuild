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
constexpr float kZero = 0.0f;
constexpr float kOne = 1.0f;
constexpr float kStatBase = 25.0f;
constexpr float kTwelve = 12.0f;
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);       // 0.01
constexpr float kFactorBase = 15.0f;
constexpr float kPointThree = std::bit_cast<float>(0x3E99999Au);    // 0.3
constexpr float kPointSeven = std::bit_cast<float>(0x3F333333u);    // 0.7
constexpr float kPointTwo = std::bit_cast<float>(0x3E4CCCCDu);      // 0.2
constexpr int kDamageWords = 7;   // words 1..7 of a damage / protection block
constexpr float kPointEight = std::bit_cast<float>(0x3F4CCCCDu);    // 0.8

// Skill levels 21 and 22 index the level table from these bases.
constexpr int kLevelBase6E8 = 0x3E;
constexpr int kLevelBase6E9 = 0x41;

// The stat range GetFactors divides by (a static initialiser: 35 - 15).
constexpr float kFactorStatRange = /*007AF280*/ 20.0f;

// The skill level record `index`, or null when there is none.
const SkillLevelRecord* LevelRecord(std::uint32_t index) {
    return unitdb::SkillLevelAt(index);
}

} // namespace

double UnitStats::StrengthRatio() { return stat10 / GameLogicSettings().hpVal[0]; }

double UnitStats::DexterityRatio() { return stat18 / GameLogicSettings().mpVal[0]; }

double UnitStats::Period20() { return kStatBase / stat20; }

float UnitStats::AttributeBase(int index) { return static_cast<float>(attributes[index]); }

double UnitStats::AttributeWithBonus(int index) {
    switch (index) {
    case 0: return (stat18 - kStatBase) + static_cast<float>(attributes[0]);
    case 1: return (stat18 - kStatBase) + static_cast<float>(attributes[1]);
    case 2: return (stat20 - kStatBase) + static_cast<float>(attributes[2]);
    default: return kZero;
    }
}

float UnitStats::SkillBase(int index) { return static_cast<float>(skills[index]); }

double UnitStats::SkillWithBonus(std::uint32_t index) {
    float skill = static_cast<float>(skills[index]);
    // The first letter of the skill's first stat name picks the stat; a missing record faults.
    const SkillRecord* skillRecord = unitdb::SkillAt(index);
    const char* name = skillRecord->statNames.begin()[0].c_str();
    switch (*name) {
    case 'd': return (stat18 - kStatBase) + skill;
    case 'i': return (stat20 - kStatBase) + skill;
    case 's': return (stat10 - kStatBase) + skill;
    default: return skill;
    }
}

float UnitStats::WeaponSkill(int type) {
    if (type < 5) return static_cast<float>(skills[0]);
    if (type == 5 || type == 6) return static_cast<float>(skills[1]);
    if (type == 8) return static_cast<float>(skills[0]);
    return kZero;
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
    return kZero;
}

double UnitStats::Value6E8() {
    double strength = stat10 * kTwelve;   // kept on the FPU stack
    std::uint8_t level = skillLevels[0x6E8 - 0x6D3];
    if (level != 0) {
        const ::SkillLevelRecord* record = LevelRecord(level + kLevelBase6E8);
        if (record) {
            return (record->Value() * kPercent + kOne) * strength;
        }
    }
    return (kZero * kPercent + kOne) * strength;
}

double UnitStats::Value6E9() {
    double base = GameLogicSettings().backstabAdd;
    std::uint8_t level = skillLevels[0x6E9 - 0x6D3];
    if (level != 0) {
        const ::SkillLevelRecord* record = LevelRecord(level + kLevelBase6E9);
        if (record) {
            return record->Value() * kPercent + base;
        }
    }
    return kZero * kPercent + base;
}

double UnitStats::Value6E9Arg(int) { return Value6E9(); }

double UnitStats::Reduce(float value) {
    double taken = kZero;   // accumulated on the FPU stack
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind < 1) continue;
        if (slot.current >= kZero) {
            taken = (slot.maximum - slot.current) / slot.maximum * slot.factor * value + taken;
        } else {
            taken = value * slot.factor + taken;   // also NaN
        }
    }
    return value - taken;   // unrounded
}

double UnitStats::ReduceKind1(float value) {
    double taken = kZero;
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind == 1) taken = value * slot.factor + taken;
    }
    return value - taken;   // unrounded
}

void UnitStats::GetFactors(float* a, float* b, int) {
    double x = (stat10 - kFactorBase) / kFactorStatRange;   // both kept on the FPU stack
    double y = kOne - (stat18 - kFactorBase) / kFactorStatRange;
    *a = static_cast<float>(y * kPointThree + x * kPointSeven);
    *b = static_cast<float>((y * kPointEight + x * kPointTwo) * kPointEight + kPointTwo);
}

BOOL LockKey::Opens(const LockKey* lock) const {
    std::uint32_t common = lock->mask & mask;
    if (common == 0) return 0;
    if ((common & 8) == 8 && id == lock->id) return 1;
    if ((common & 7) == 0) return 0;
    if (ei::rebirth::Enabled(ei::rebirth::Patch::FixLeversActivate)) {
        // Rebirth: levels compared signed; a lock of level 0 gives the key's level + 1, so a key of level -1
        // (0xFFFFFFFF) no longer opens it.
        if (lock->level == 0) return static_cast<BOOL>(level + 1);
        return static_cast<std::int32_t>(level) < static_cast<std::int32_t>(lock->level) ? 0 : 1;
    }
    return level >= lock->level ? 1 : 0;
}

std::uint32_t __fastcall ResetDamageInfo(DamageInfo* info) {
    constexpr std::uint32_t kLarge = 0x7C23D70A;
    std::memset(info, 0, 0x3C);
    for (float& weight : info->typeWeights) weight = kOne;
    info->multiplier = kOne;
    info->weaponValue = 0;
    info->attackRoll = kLarge;
    std::memcpy(&info->amount, &kLarge, sizeof(kLarge));
    info->location = 6;
    info->hitKind = 0;
    return kLarge;
}

BOOL __fastcall HasElementalDamage(const DamageInfo* info) {
    EI_NAN_AUDIT(info->typeWeights[3], info->typeWeights[4], info->typeWeights[5]);
    if (info->typeWeights[3] == kZero && info->typeWeights[4] == kZero && info->typeWeights[5] == kZero) {
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
std::uint16_t __fastcall MaxDamage(const std::uint16_t* damage) {
    std::uint16_t largest = 0;
    for (int i = 1; i <= kDamageWords; ++i)
        if (largest <= damage[i]) largest = damage[i];
    return largest;
}
