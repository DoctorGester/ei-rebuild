// Creature statistics: attribute, skill and skill level lookups over UnitStats, and small helpers
// of the hit (DamageInfo) and lock records. See unit.h.
#include "unit.h"

#include "spell.h"

#include "ei/hook.h"

#include <bit>
#include <cstring>

// /fp:fast reassociates and distributes (e.g. (a - 25) + b became (a + b) - 25); the formulas here
// must keep the original's order of operations.
#pragma float_control(precise, on)

namespace {
constexpr float kZero = 0.0f;                                       // 0073B858
constexpr float kOne = 1.0f;                                        // 0073B84C
constexpr float kStatBase = 25.0f;                                  // 0073E124
constexpr float kTwelve = 12.0f;                                    // 0073E138
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);       // 0073C548 (0.01)
constexpr float kFactorBase = 15.0f;                                // 0073E10C
constexpr float kPointThree = std::bit_cast<float>(0x3E99999Au);    // 0073C54C (0.3)
constexpr float kPointSeven = std::bit_cast<float>(0x3F333333u);    // 0073C538 (0.7)
constexpr float kPointTwo = std::bit_cast<float>(0x3E4CCCCDu);      // 0073BAD4 (0.2)
constexpr float kPointEight = std::bit_cast<float>(0x3F4CCCCDu);    // 0073CCA8 (0.8)

// Skill levels 21 and 22 (+0x6E8, +0x6E9) index the level table from these bases.
constexpr int kLevelBase6E8 = 0x3E;
constexpr int kLevelBase6E9 = 0x41;

float Global7C5430() { return *reinterpret_cast<const float*>(0x007C5430); }
float Global7AF280() { return *reinterpret_cast<const float*>(0x007AF280); }

const std::uint8_t* Bytes(const UnitStats* stats) { return reinterpret_cast<const std::uint8_t*>(stats); }

// +0x34 of the skill level record `index`, or null when there is none.
const char* LevelRecord(std::uint32_t index) {
    return unitdb::SkillLevels().At(index, unitdb::kSkillLevelSize);
}

bool IsZeroOrNaN(float f) { return !(f < kZero || f > kZero); }

// +0x34 of a level record (unsigned), loaded exactly like the original's FILD of a zero-extended
// qword. A plain uint32 conversion adds 2^32 to values >= 2^31 with an FADD, which the FPU's
// precision control rounds; the volatile int64 keeps it a single exact FILD.
double LevelValue(const char* record) {
    volatile std::int64_t wide = Field<std::uint32_t>(record, 0x34);
    return static_cast<double>(wide);
}
} // namespace

float UnitStats::Period20() { return kStatBase / stat20; }
EI_REPLACE_METHOD(0x00523CD0, UnitStats::Period20);

float UnitStats::AttributeBase(int index) { return static_cast<float>(Bytes(this)[0x6C8 + index]); }
EI_REPLACE_METHOD(0x00525DF0, UnitStats::AttributeBase);

float UnitStats::AttributeWithBonus(int index) {
    switch (index) {
    case 0: return (stat18 - kStatBase) + static_cast<float>(attributes[0]);
    case 1: return (stat18 - kStatBase) + static_cast<float>(attributes[1]);
    case 2: return (stat20 - kStatBase) + static_cast<float>(attributes[2]);
    default: return kZero;
    }
}
EI_REPLACE_METHOD(0x00525E10, UnitStats::AttributeWithBonus);

float UnitStats::SkillBase(int index) { return static_cast<float>(Bytes(this)[0x6CB + index]); }
EI_REPLACE_METHOD(0x00527030, UnitStats::SkillBase);

float UnitStats::SkillWithBonus(std::uint32_t index) {
    float skill = static_cast<float>(Bytes(this)[0x6CB + index]);
    // The skill's name (via the record's +0x0C) picks the stat; a missing record faults.
    const char* record = unitdb::Skills().At(index, unitdb::kSkillSize);
    const char* const* name = Field<const char* const*>(record, 0x0C);
    switch (**name) {
    case 'd': return (stat18 - kStatBase) + skill;
    case 'i': return (stat20 - kStatBase) + skill;
    case 's': return (stat10 - kStatBase) + skill;
    default: return skill;
    }
}
EI_REPLACE_METHOD(0x00527050, UnitStats::SkillWithBonus);

float UnitStats::WeaponSkill(int type) {
    if (type < 5) return static_cast<float>(skills[0]);
    if (type == 5 || type == 6) return static_cast<float>(skills[1]);
    if (type == 8) return static_cast<float>(skills[0]);
    return kZero;
}
EI_REPLACE_METHOD(0x00526780, UnitStats::WeaponSkill);

char* UnitStats::SkillLevelRecord(int index) {
    std::uint8_t level = Bytes(this)[0x6D3 + index];
    if (level == 0) return nullptr;
    return unitdb::SkillLevels().At(static_cast<std::uint32_t>(index * 3 - 1 + level), unitdb::kSkillLevelSize);
}
EI_REPLACE_METHOD(0x00528270, UnitStats::SkillLevelRecord);

double UnitStats::SkillLevelValue(int index) {
    std::uint8_t level = Bytes(this)[0x6D3 + index];
    if (level != 0) {
        const char* record = LevelRecord(static_cast<std::uint32_t>(index * 3 - 1 + level));
        if (record) return LevelValue(record);
    }
    return kZero;
}
EI_REPLACE_METHOD(0x005267E0, UnitStats::SkillLevelValue);

float UnitStats::Value6E8() {
    double strength = stat10 * kTwelve;   // kept on the FPU stack in the original
    std::uint8_t level = skillLevels[0x6E8 - 0x6D3];
    if (level != 0) {
        const char* record = LevelRecord(level + kLevelBase6E8);
        if (record) {
            return static_cast<float>(
                (LevelValue(record) * kPercent + kOne) * strength);
        }
    }
    return static_cast<float>((kZero * kPercent + kOne) * strength);
}
EI_REPLACE_METHOD(0x00524280, UnitStats::Value6E8);

float UnitStats::Value6E9() {
    double base = Global7C5430();
    std::uint8_t level = skillLevels[0x6E9 - 0x6D3];
    if (level != 0) {
        const char* record = LevelRecord(level + kLevelBase6E9);
        if (record) {
            return static_cast<float>(LevelValue(record) * kPercent + base);
        }
    }
    return static_cast<float>(kZero * kPercent + base);
}
EI_REPLACE_METHOD(0x00525CD0, UnitStats::Value6E9);

float UnitStats::Value6E9Arg(int) { return Value6E9(); }
EI_REPLACE_METHOD(0x00525D60, UnitStats::Value6E9Arg);

float UnitStats::Reduce(float value) {
    double taken = kZero;   // accumulated on the FPU stack in the original
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind < 1) continue;
        if (!(slot.current >= kZero)) {
            taken = value * slot.factor + taken;
        } else {
            taken = (slot.maximum - slot.current) / slot.maximum * slot.factor * value + taken;
        }
    }
    return static_cast<float>(value - taken);
}
EI_REPLACE_METHOD(0x005259B0, UnitStats::Reduce);

float UnitStats::ReduceKind1(float value) {
    double taken = kZero;
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind == 1) taken = value * slot.factor + taken;
    }
    return static_cast<float>(value - taken);
}
EI_REPLACE_METHOD(0x00524020, UnitStats::ReduceKind1);

void UnitStats::GetFactors(float* a, float* b, int) {
    double x = (stat10 - kFactorBase) / Global7AF280();   // both kept on the FPU stack
    double y = kOne - (stat18 - kFactorBase) / Global7AF280();
    *a = static_cast<float>(y * kPointThree + x * kPointSeven);
    *b = static_cast<float>((y * kPointEight + x * kPointTwo) * kPointEight + kPointTwo);
}
EI_REPLACE_METHOD(0x00528840, UnitStats::GetFactors);

BOOL LockKey::Opens(const LockKey* lock) {
    std::uint32_t common = lock->mask & mask;
    if (common == 0) return 0;
    if ((common & 8) == 8 && id == lock->id) return 1;
    if ((common & 7) == 0) return 0;
    return level >= lock->level ? 1 : 0;
}
EI_REPLACE_METHOD(0x00522A80, LockKey::Opens);

std::uint32_t __fastcall ResetDamageInfo(DamageInfo* info) {
    constexpr std::uint32_t kLarge = 0x7C23D70A;
    std::memset(info, 0, 0x3C);
    for (float& weight : info->typeWeights) weight = kOne;
    info->multiplier = kOne;
    info->unknown_1C = 0;
    info->unknown_20 = kLarge;
    std::memcpy(&info->amount, &kLarge, sizeof(kLarge));
    info->unknown_2C = 6;
    info->unknown_30 = 0;
    return kLarge;
}
EI_REPLACE(0x00522B40, ResetDamageInfo);

BOOL __fastcall HasElementalDamage(const DamageInfo* info) {
    if (IsZeroOrNaN(info->typeWeights[3]) && IsZeroOrNaN(info->typeWeights[4]) &&
        IsZeroOrNaN(info->typeWeights[5])) {
        return 0;
    }
    return 1;
}
EI_REPLACE(0x00522B80, HasElementalDamage);
