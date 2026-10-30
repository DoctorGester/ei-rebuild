// What learning a skill level or raising a school costs; the interface's
// skill pages call them. With the level record lookup they use, so ei_vectors links
// them without the rest of the skill database. See unit.h (UnitStats::SkillPrice) and
// docs/notes/units.md ("Statistics in play"). Float maths at the main thread's 24-bit precision; the powers are the
// game CRT's _CIpow (libcrt::CiPow), the logarithm the original's FYL2X (pc24::Log10).
#include "logic_settings.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/pc24.h"

#include <cmath>
#include <cstdint>
#include "lib/rounding.h"

namespace {
constexpr double kWorthBase = 1.61;   // a double
constexpr float kTwoDigitsBelow = 4.0f; // a first digit below this keeps two digits
constexpr float kRoundUpTo = 10.0f;     // ... unless the price is at most this
constexpr float kTenth = 0.1f;
constexpr int kKnownLevels = 0x24;      // the skill level bytes summed (UnitStats::skillLevels)

// What a level value is worth (the same curve as RaiseSkill's): the game's settings "Skill Val 1"
// (the scale) and "Skill Val 2" (the divisor), read at the time of the call. The int over the divisor rounds once.
float Worth(int value) {
    const LogicSettings& settings = g_logicSettings;
    const float exponent = pc24::Div(value, settings.skillVal[1]);
    return static_cast<float>(libcrt::CiPow(kWorthBase, exponent)) * settings.skillVal[0];
}

} // namespace

int SkillDatabase::IndexOfLevel(const SkillLevelRecord* record) {
    for (const SkillLevelRecord& level : levels)
        if (&level == record) return static_cast<int>(&level - levels.data());
    return -1;
}

// What raising a school from level value `from` to `to` costs: the worths' difference, truncated.
int SchoolPrice(int from, int to) {
    const float upper = Worth(to);
    const float lower = Worth(from);
    return lib::Truncate(upper - lower);
}

int UnitStats::SkillPrice(const ::SkillLevelRecord* level) {
    int index = g_skillDatabase.IndexOfLevel(level);
    float worth = Worth(level->WorthExponent());
    float below;
    if (index % 3 > 0) {
        // The level below in the same skill: the record before it (read unchecked: a missing one faults).
        const ::SkillLevelRecord* previous = unitdb::SkillLevelAt(static_cast<std::uint32_t>(index - 1));
        below = Worth(previous->WorthExponent());
    } else {
        below = Worth(0);
    }
    float cost = static_cast<float>(lib::Truncate(worth - below));   // truncated, then stored as a float

    std::uint8_t known = 0;   // a byte sum (wraps)
    for (int i = 0; i < kKnownLevels; ++i) known = static_cast<std::uint8_t>(known + skillLevels[i]);
    float price = static_cast<float>(libcrt::CiPow(g_logicSettings.perkPowerBase, known)) * cost;

    // The power of ten of the first digit: the 64-bit log10 stored to a double for the CRT's floor, then _CIpow.
    float power = static_cast<float>(libcrt::CiPow(10.0, std::floor(pc24::Log10(price).hi)));
    float leading = price / power;
    if (leading >= kTwoDigitsBelow) return lib::Truncate(std::floor(leading + 0.5f) * power);
    // Below 4, or NaN: a price of 0 gives power 0 and 0 / 0, which this branch returns as 0 (it
    // occurs; the plain leading < 4 would return the NaN's integer conversion).
    EI_NAN_AUDIT(power);
    if (power <= kRoundUpTo) return lib::Truncate(std::ceil(price));
    power = power * kTenth;
    leading = price / power;
    return lib::Truncate(std::floor(leading + 0.5f) * power);
}

