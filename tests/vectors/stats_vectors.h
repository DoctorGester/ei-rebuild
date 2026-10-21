#pragma once

// Payloads of the "stats" family (vectors.h): the creature statistics, lock and damage word formulas of
// src/game/unit_stats.cpp, unit_skill_price.cpp, unit_params_protection.cpp, unit_params_skill.cpp and
// unit_hit_location.cpp. Recorded by record_stats.cpp, replayed by replay_stats.cpp.
// A StatView holds the fields of a UnitStats the formulas read; the recorder fills only those the
// function in question reads and leaves the rest zero, so equal calls give equal bytes.

#include "../../src/game/spell.h"
#include "../../src/game/unit.h"

#include <cstdint>

namespace ei::vectors::stats {

struct SlotView {
    std::int32_t kind;
    float current, maximum, factor;
};
struct StatView {
    float stat10, stat18, stat20;
    std::uint8_t attributes[3];
    std::uint8_t skills[8];
    std::uint8_t pad;
    SlotView slots[6];
};

// The parts of a StatView a function reads.
enum View : unsigned { kStat10 = 1, kStat18 = 2, kStat20 = 4, kAttributes = 8, kSkills = 16, kSlots = 32 };

// Period20, AttributeBase, AttributeWithBonus, SkillBase, WeaponSkill: `arg` the index or type; a float
// result is stored as the double of the same value.
struct IndexCall {
    StatView stats;
    std::int32_t arg;
    std::uint32_t pad;
    double result;
};
// Reduce, ReduceKind1.
struct ReduceCall {
    StatView stats;
    float value;
    std::uint32_t pad;
    double result;
};
struct OpensCall {         // LockKey::Opens (Rebirth's FixLeversActivate off)
    LockKey key, lock;
    std::int32_t result;
};
struct ResetDamageCall {   // ResetDamageInfo: what it wrote and returned
    DamageInfo after;
    std::uint32_t result;
};
struct ElementalCall {     // HasElementalDamage: the weights of types 3, 4 and 5
    float weights[3];
    std::int32_t result;
};
struct ExcessCall {        // ExcessDamage: words 1..7 of both blocks; the result is AX
    std::uint16_t damage[7], protection[7];
    std::uint32_t result;
};
struct MaxCall {           // MaxDamage: words 1..7
    std::uint16_t damage[7];
    std::uint16_t result;
};

// ---- SV4d: the formulas that read the game's settings and the skill database (src/game/game_data.h).
// The payload carries what they read there; the replay writes it to the stand-in.

// A skill level record a formula reads: its index among the level records (RecordTable::RecordAt's
// argument), whether the table had it (index below its count), and its value (values[7]) and worth
// exponent (values[6]).
struct LevelView {
    std::uint32_t index;
    std::int32_t exists;
    std::uint32_t value;
    std::int32_t exponent;
};
// SkillLevelValue (`index` the skill, `level` its level byte), Value6E8 (stat10, the byte at
// +0x6E8) and Value6E9 (`setting` the setting backstabAdd, the byte at +0x6E9).
struct LevelCall {
    std::int32_t index;
    float stat10, setting;
    std::uint8_t level, pad[3];
    LevelView record;
    double result;
};
// SkillWithBonus: the three stats, the skill bytes, and the first letter of the skill's first stat
// name (its record exists and names at least one stat).
struct SkillBonusCall {
    float stat10, stat18, stat20;
    std::uint8_t skills[8];
    std::uint32_t index;
    char letter, pad[3];
    std::uint32_t pad2;
    double result;
};
// StrengthRatio (stat10, "HP Val 1") and DexterityRatio (stat18, "MP Val 1").
struct RatioCall {
    float stat, setting;
    double result;
};
// GetFactors: stat10, stat18, the stat range (007AF280), what it wrote to a and b (one float when the
// two pointers were the same: `aliased`).
struct FactorsCall {
    float stat10, stat18, range;
    float a, b;
    std::int32_t aliased;
};
// SchoolPrice: the levels and the settings "Skill Val 1" / "Skill Val 2".
struct SchoolPriceCall {
    std::int32_t from, to;
    float skillVal[2];
    std::int32_t result;
};
// SkillPrice: the record's index among the level records (-1: not among them), its worth exponent
// and the one of the record before it (when index % 3 > 0), the skill level bytes, the settings
// "Skill Val 1" / "Skill Val 2" and "Perk Power Base".
struct SkillPriceCall {
    std::int32_t index, exponent, previousExponent;
    std::uint8_t skillLevels[0x24];
    float skillVal[2], perkPowerBase;
    std::int32_t result;
};
// SkillDatabase::IndexOfLevel: the number of level records and where the record is, in bytes from
// the first.
struct IndexOfLevelCall {
    std::uint32_t count;
    std::int32_t offset;
    std::int32_t result;
};
// UnitParams::ProtectionBlock. A slot's `weight` is the table entry (unitdb::kSlotWeights) at its type, for the
// slots a keyed call reads; `factor` the setting "Absorption" at `difficulty`, for a call that is not.
struct ProtectionSlot {
    std::int32_t type, kind;
    std::int16_t protection[7];
    std::uint16_t pad;
    float weight;
};
struct ProtectionCall {
    ProtectionSlot slots[6];
    std::int16_t vitals[7];
    std::uint8_t keyed, pad;
    std::int32_t difficulty;
    float factor;
    float out[7];
};
// UnitParams::WeaponSkillBonus (types 0..8): the parameter bytes it reads, the level byte and record
// of skill `type` (types below 7), the first letters of skills 0 and 1's first stat names, and the
// weapon's skill bonus bytes (`weapon` 0: no weapon).
struct WeaponBonusCall {
    std::int32_t type;
    std::uint8_t stat10, stat18, attackRate, level;
    std::uint8_t skills[2];
    char letters[2];
    std::uint8_t weapon, bonus[2], pad;
    LevelView record;
    double result;
};
// UnitStats::PickLocation (locations 0..5): the six slots' kinds; `drew` 1 when it drew from the
// game's generator (without a refill), `draw` the value.
struct PickLocationCall {
    std::int32_t kinds[6];
    std::int32_t location;
    std::int32_t drew;
    std::uint32_t draw;
    std::int32_t result;
};

static_assert(sizeof(LevelView) == 16 && sizeof(LevelCall) == 40 && sizeof(SkillBonusCall) == 40 &&
              sizeof(RatioCall) == 16 && sizeof(FactorsCall) == 24 && sizeof(SchoolPriceCall) == 20 &&
              sizeof(SkillPriceCall) == 64 && sizeof(IndexOfLevelCall) == 12);
static_assert(sizeof(ProtectionSlot) == 28 && sizeof(ProtectionCall) == 220 && sizeof(WeaponBonusCall) == 40 &&
              sizeof(PickLocationCall) == 40);

static_assert(sizeof(StatView) == 120 && sizeof(IndexCall) == 136 && sizeof(ReduceCall) == 136);
static_assert(sizeof(OpensCall) == 28 && sizeof(ResetDamageCall) == 64&& sizeof(ElementalCall) == 16 && sizeof(ExcessCall) == 32 &&
              sizeof(MaxCall) == 16);

} // namespace ei::vectors::stats
