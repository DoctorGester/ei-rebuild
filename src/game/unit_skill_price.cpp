// What learning a skill level or raising a school costs; the interface's
// skill pages call them. With the level record lookup they use, so ei_vectors links
// them without the rest of the skill database. See unit.h (UnitStats::SkillPrice) and
// docs/notes/units.md ("Statistics in play"). Plain C++ float maths (rule 7).
#include "logic_settings.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/nan_audit.h"

#include <cmath>
#include <cstdint>

namespace {
constexpr double kWorthBase = 1.61;
constexpr float kTwoDigitsBelow = 4.0f; // a first digit below this keeps two digits
constexpr float kRoundUpTo = 10.0f;     // ... unless the price is at most this
constexpr float kTenth = 0.1f;
constexpr int kKnownLevels = 0x24;      // the skill level bytes summed (UnitStats::skillLevels)

// What a level value is worth (the same curve as RaiseSkill's): the game's settings "Skill Val 1"
// (the scale) and "Skill Val 2" (the divisor), read at the time of the call.
double Worth(int value) {
    const LogicSettings& settings = g_logicSettings;
    return std::pow(kWorthBase, static_cast<double>(value) / settings.skillVal[1]) * settings.skillVal[0];
}
} // namespace

int SkillDatabase::IndexOfLevel(const SkillLevelRecord* record) {
    for (const SkillLevelRecord& level : levels)
        if (&level == record) return static_cast<int>(&level - levels.data());
    return -1;
}

// What raising a school from level value `from` to `to` costs: the worths' difference, truncated.
int SchoolPrice(int from, int to) {
    const double upper = Worth(to);
    const double lower = Worth(from);
    return static_cast<int>(upper - lower);
}

int UnitStats::SkillPrice(const ::SkillLevelRecord* level) {
    int index = g_skillDatabase->IndexOfLevel(level);
    double worth = Worth(level->WorthExponent());
    double below;
    if (index % 3 > 0) {
        // The level below in the same skill: the record before it (read unchecked: a missing one faults).
        const ::SkillLevelRecord* previous = unitdb::SkillLevelAt(static_cast<std::uint32_t>(index - 1));
        below = Worth(previous->WorthExponent());
    } else {
        below = Worth(0);
    }
    float cost = static_cast<float>(static_cast<int>(worth - below));   // truncated, then stored as a float

    std::uint8_t known = 0;   // a byte sum (wraps)
    for (int i = 0; i < kKnownLevels; ++i) known = static_cast<std::uint8_t>(known + skillLevels[i]);
    // Stored to a float and to a double of the same value (the product is rounded at the game's
    // 24-bit precision before either store).
    float price = static_cast<float>(std::pow(static_cast<double>(g_logicSettings.perkPowerBase), known) * cost);

    // The power of ten of the first digit (log10, the CRT's floor and _CIpow).
    float power = static_cast<float>(std::pow(10.0, std::floor(x87::flog10(static_cast<double>(price)))));
    float leading = price / power;
    if (leading >= kTwoDigitsBelow) return static_cast<int>(std::floor(static_cast<double>(leading + 0.5f)) * power);
    // Below 4, or NaN: a price of 0 gives power 0 and 0 / 0, which this branch returns as 0 (it
    // occurs; the plain leading < 4 would return the NaN's integer conversion).
    EI_NAN_AUDIT(power);
    if (power <= kRoundUpTo) return static_cast<int>(std::ceil(static_cast<double>(price)));
    power = power * kTenth;
    leading = price / power;
    return static_cast<int>(std::floor(static_cast<double>(leading + 0.5f)) * power);
}
