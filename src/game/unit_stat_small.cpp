// Small functions of the unit statistics and databases (A-U12): two stat ratios, the packed
// vitals' and parameters' network fields, the damage words' sums, a school's price, the attack
// parameters' difficulty scale and the databases' record lookups. Plain C++ float maths (rule 7).
#include "combat.h"
#include "net_delta.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"

#include "ei/hook.h"

#include <cmath>

namespace {
constexpr double kWorthBase = 1.61;   // 0073E140
// The game's settings (globals read at the time of the call).
constexpr std::uintptr_t kHitPointStatDivisor = std::uintptr_t(0x007C5454);
constexpr std::uintptr_t kManaStatDivisor = std::uintptr_t(0x007C5468);
constexpr std::uintptr_t kWorthScale = std::uintptr_t(0x007C547C);
constexpr std::uintptr_t kWorthDivisor = std::uintptr_t(0x007C5480);
constexpr std::uintptr_t kDifficulty = std::uintptr_t(0x0079BEC0);
constexpr std::uintptr_t kAttackFactors = std::uintptr_t(0x007C54AC);    // a float per difficulty
constexpr std::uintptr_t kDefenceFactors = std::uintptr_t(0x007C54B8);
constexpr int kDamageWords = 7;   // words 1..7 of a damage / protection block

float Global(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }
int Difficulty() { return *reinterpret_cast<const int*>(kDifficulty); }

// What a level value is worth (the curve of UnitStats::SkillPrice).
double Worth(int value) {
    return std::pow(kWorthBase, static_cast<double>(value) / Global(kWorthDivisor)) * Global(kWorthScale);
}

// Element `index` of the records between `begin` and `end`, or null past the end (the count is a
// signed division, the comparison unsigned, as the original's).
template <typename T>
T* RecordAt(T* begin, T* end, std::uint32_t index) {
    return index < static_cast<std::uint32_t>(end - begin) ? begin + index : nullptr;
}
} // namespace

// --- Statistics -----------------------------------------------------------------------------------

double UnitStats::StrengthRatio() { return stat10 / Global(kHitPointStatDivisor); }
EI_REPLACE_METHOD(0x00523CB0, UnitStats::StrengthRatio);

double UnitStats::DexterityRatio() { return stat18 / Global(kManaStatDivisor); }
EI_REPLACE_METHOD(0x00523CC0, UnitStats::DexterityRatio);

// 00523700 / 00523710 (fastcall: ECX the block, EDX the field): a map object's packed vitals
// written to / read from its network field. 00523720 / 00523730: a unit's packed parameters.
void __fastcall WritePackedVitals(const UnitVitalsPacked* vitals, DeltaField* field) {
    field->WriteDelta(vitals, sizeof(UnitVitalsPacked));
}
EI_REPLACE(0x00523700, WritePackedVitals);
void __fastcall ReadPackedVitals(UnitVitalsPacked* vitals, DeltaField* field) {
    field->ReadDelta(vitals, sizeof(UnitVitalsPacked));
}
EI_REPLACE(0x00523710, ReadPackedVitals);
void __fastcall WritePackedParams(const UnitParams* params, DeltaField* field) {
    field->WriteDelta(params, sizeof(UnitParams));
}
EI_REPLACE(0x00523720, WritePackedParams);
void __fastcall ReadPackedParams(UnitParams* params, DeltaField* field) {
    field->ReadDelta(params, sizeof(UnitParams));
}
EI_REPLACE(0x00523730, ReadPackedParams);

// 00522AE0 (thiscall on the damage words, RET 4). The sum over words 1..7 of what exceeds the
// matching protection word (a difference with its top bit set counts 0), as 16 bits; the
// original leaves leftovers in the upper half of EAX (callers read AX).
std::uint32_t __fastcall ExcessDamage(const std::uint16_t* damage, void*, const std::uint16_t* protection) {
    std::uint16_t sum = 0;
    for (int i = 1; i <= kDamageWords; ++i) {
        const std::uint16_t difference = static_cast<std::uint16_t>(damage[i] - protection[i]);
        if (!(difference & 0x8000)) sum = static_cast<std::uint16_t>(sum + difference);
    }
    return sum;
}
EI_REPLACE(0x00522AE0, ExcessDamage);

// 00522B20 (thiscall on the damage words). The largest of words 1..7 (unsigned), in AX.
std::uint16_t __fastcall MaxDamage(const std::uint16_t* damage) {
    std::uint16_t largest = 0;
    for (int i = 1; i <= kDamageWords; ++i)
        if (largest <= damage[i]) largest = damage[i];
    return largest;
}
EI_REPLACE(0x00522B20, MaxDamage);

// 00527FF0 (fastcall). What raising a school from level value `from` to `to` costs: the worths'
// difference, truncated.
int __fastcall SchoolPrice(int from, int to) {
    const double upper = Worth(to);
    const double lower = Worth(from);
    return static_cast<int>(upper - lower);
}
EI_REPLACE(0x00527FF0, SchoolPrice);

void AttackParams::ScaleForDifficulty(int keyed) {
    if (keyed != 0) return;
    attack = static_cast<int>(attack * reinterpret_cast<const float*>(kAttackFactors)[Difficulty()]);
    defence =
        static_cast<int>(static_cast<float>(defence) * reinterpret_cast<const float*>(kDefenceFactors)[Difficulty()]);
}
EI_REPLACE_METHOD(0x0052E0D0, AttackParams::ScaleForDifficulty);

// --- Database records ---------------------------------------------------------------------------------

SkillRecord* SkillDatabase::SkillAt(std::uint32_t index) { return RecordAt(skills.begin(), skills.end(), index); }
EI_REPLACE_METHOD(0x00526FF0, SkillDatabase::SkillAt);

SkillLevelRecord* SkillDatabase::LevelAt(std::uint32_t index) { return RecordAt(levels.begin(), levels.end(), index); }
EI_REPLACE_METHOD(0x00524FA0, SkillDatabase::LevelAt);

// 00539280 (thiscall on the unit database's model vector, RET 4): model `index`, or null.
UnitModelRecord* __fastcall UnitModelAt(UnitModelRecord** models, void*, std::uint32_t index) {
    return RecordAt(models[0], models[1], index);
}
EI_REPLACE(0x00539280, UnitModelAt);

// 0053AD30 (thiscall on the prototype table, RET 4): prototype `index`, or null.
UnitPrototypeRecord* __fastcall UnitPrototypeAt(UnitPrototypeTable* table, void*, std::uint32_t index) {
    return RecordAt(reinterpret_cast<UnitPrototypeRecord*>(table->begin),
                    reinterpret_cast<UnitPrototypeRecord*>(table->end), index);
}
EI_REPLACE(0x0053AD30, UnitPrototypeAt);
