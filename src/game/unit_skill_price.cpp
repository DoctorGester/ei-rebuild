// What learning a skill level costs (00528050; the interface's skill pages call it). See unit.h
// (UnitStats::SkillPrice) and docs/notes/units.md ("Sixth round"). Plain C++ float maths (rule 7).
#include "item_db.h"   // Field
#include "unit.h"

#include "ei/hook.h"

#include <cmath>
#include <cstdint>

namespace {
constexpr double kWorthBase = 1.61;     // 0073E140
constexpr float kTwoDigitsBelow = 4.0f; // 0073BAA0: a first digit below this keeps two digits
constexpr float kRoundUpTo = 10.0f;     // 0073BAAC: ... unless the price is at most this
constexpr float kTenth = 0.1f;          // 0073B850
constexpr float kHalf = 0.5f;           // 0073B86C
constexpr int kLevelValue = 0x30;       // skill level record: the level's worth exponent (int)
constexpr int kKnownLevels = 0x24;      // the skill level bytes summed (+0x6D3..+0x6F6)

// The game's settings (globals read at the time of the call): the worth scale and divisor of a
// level, the price growth per known skill level.
constexpr std::uintptr_t kWorthScale = 0x007C547C;
constexpr std::uintptr_t kWorthDivisor = 0x007C5480;
constexpr std::uintptr_t kPriceGrowth = 0x007C5484;
float Global(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }

constexpr std::uintptr_t kSkillDatabase = 0x007AF268;
// 0051FF00 (not rewritten): the index of `record` among the skill database's level records, or -1.
__declspec(naked) int __fastcall SkillRecordIndex(const void*, void*, const void*) EI_ORIGINAL(0x0051FF00)

// What a level value is worth (the same curve as RaiseSkill's).
double Worth(int value) {
    return std::pow(kWorthBase, static_cast<double>(value) / Global(kWorthDivisor)) * Global(kWorthScale);
}
} // namespace

int UnitStats::SkillPrice(const char* record) {
    int index = SkillRecordIndex(reinterpret_cast<const void*>(kSkillDatabase), nullptr, record);
    double worth = Worth(Field<int>(record, kLevelValue));
    double below;
    if (index % 3 > 0) {
        // The level below in the same skill: the record before it (read unchecked: a missing one
        // faults, as in the original).
        const char* previous =
            unitdb::SkillLevels().At(static_cast<std::uint32_t>(index - 1), unitdb::kSkillLevelSize);
        below = Worth(Field<int>(previous, kLevelValue));
    } else {
        below = Worth(0);
    }
    float cost = static_cast<float>(static_cast<int>(worth - below));   // _ftol, then FILD; FSTP float

    std::uint8_t known = 0;   // a byte sum (wraps)
    for (int i = 0; i < kKnownLevels; ++i) known = static_cast<std::uint8_t>(known + skillLevels[i]);
    // Stored to a float and to a double of the same value (the product is rounded at the game's
    // 24-bit precision before either store).
    float price = static_cast<float>(std::pow(static_cast<double>(Global(kPriceGrowth)), known) * cost);

    // The power of ten of the first digit (FLDLG2; FYL2X, the CRT's floor and _CIpow).
    float power = static_cast<float>(std::pow(10.0, std::floor(std::log10(static_cast<double>(price)))));
    float leading = price / power;
    if (!(leading >= kTwoDigitsBelow)) {   // below 4, or NaN (a price of 0)
        if (!(power > kRoundUpTo)) return static_cast<int>(std::ceil(static_cast<double>(price)));
        power = power * kTenth;
        leading = price / power;
    }
    return static_cast<int>(std::floor(static_cast<double>(leading + kHalf)) * power);
}
EI_REPLACE_METHOD(0x00528050, UnitStats::SkillPrice);
