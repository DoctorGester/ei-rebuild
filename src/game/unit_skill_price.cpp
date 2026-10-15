// What learning a skill level (00528050) or raising a school (00527FF0) costs; the interface's
// skill pages call them. With the level record lookup they use (0051FF00), so ei_vectors links
// them without the rest of the skill database. See unit.h (UnitStats::SkillPrice) and
// docs/notes/units.md ("Statistics in play"). Plain C++ float maths (rule 7).
#include "logic_settings.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/hook.h"

#include <cmath>
#include <cstdint>

namespace {
constexpr double kWorthBase = 1.61;     // 0073E140
constexpr float kTwoDigitsBelow = 4.0f; // 0073BAA0: a first digit below this keeps two digits
constexpr float kRoundUpTo = 10.0f;     // 0073BAAC: ... unless the price is at most this
constexpr float kTenth = 0.1f;          // 0073B850
constexpr float kHalf = 0.5f;           // 0073B86C
constexpr int kKnownLevels = 0x24;      // the skill level bytes summed (+0x6D3..+0x6F6)

// What a level value is worth (the same curve as RaiseSkill's): the game's settings "Skill Val 1"
// (the scale) and "Skill Val 2" (the divisor), read at the time of the call.
double Worth(int value) {
    const LogicSettings& settings = GameLogicSettings();
    return std::pow(kWorthBase, static_cast<double>(value) / settings.skillVal[1]) * settings.skillVal[0];
}
} // namespace

int SkillDatabase::IndexOfLevel(const SkillLevelRecord* record) {
    for (const SkillLevelRecord* level = levels.begin(); level != levels.end(); ++level)
        if (level == record) return static_cast<int>(level - levels.begin());
    return -1;
}
EI_REPLACE_METHOD(0x0051FF00, SkillDatabase::IndexOfLevel);

// 00527FF0 (fastcall). What raising a school from level value `from` to `to` costs: the worths'
// difference, truncated.
int __fastcall SchoolPrice(int from, int to) {
    const double upper = Worth(to);
    const double lower = Worth(from);
    return static_cast<int>(upper - lower);
}
EI_REPLACE(0x00527FF0, SchoolPrice);

int UnitStats::SkillPrice(const char* record) {
    auto* level = reinterpret_cast<const ::SkillLevelRecord*>(record);
    int index = SkillDb()->IndexOfLevel(level);
    double worth = Worth(level->WorthExponent());
    double below;
    if (index % 3 > 0) {
        // The level below in the same skill: the record before it (read unchecked: a missing one
        // faults, as in the original).
        auto* previous = reinterpret_cast<const ::SkillLevelRecord*>(
            unitdb::SkillLevels().RecordAt(static_cast<std::uint32_t>(index - 1), unitdb::kSkillLevelSize));
        below = Worth(previous->WorthExponent());
    } else {
        below = Worth(0);
    }
    float cost = static_cast<float>(static_cast<int>(worth - below));   // _ftol, then FILD; FSTP float

    std::uint8_t known = 0;   // a byte sum (wraps)
    for (int i = 0; i < kKnownLevels; ++i) known = static_cast<std::uint8_t>(known + skillLevels[i]);
    // Stored to a float and to a double of the same value (the product is rounded at the game's
    // 24-bit precision before either store).
    float price = static_cast<float>(std::pow(static_cast<double>(GameLogicSettings().perkPowerBase), known) * cost);

    // The power of ten of the first digit (FLDLG2; FYL2X, the CRT's floor and _CIpow).
    float power = static_cast<float>(std::pow(10.0, std::floor(x87::flog10(static_cast<double>(price)))));
    float leading = price / power;
    if (!(leading >= kTwoDigitsBelow)) {   // below 4, or NaN (a price of 0)
        if (!(power > kRoundUpTo)) return static_cast<int>(std::ceil(static_cast<double>(price)));
        power = power * kTenth;
        leading = price / power;
    }
    return static_cast<int>(std::floor(static_cast<double>(leading + kHalf)) * power);
}
EI_REPLACE_METHOD(0x00528050, UnitStats::SkillPrice);
