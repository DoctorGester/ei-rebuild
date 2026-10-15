#pragma once

// Payloads of the "stats" family (vectors.h): the pure creature statistics, lock and damage word formulas of
// src/game/unit_stats.cpp. Recorded by record_stats.cpp, replayed by replay_stats.cpp.
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

static_assert(sizeof(StatView) == 120 && sizeof(IndexCall) == 136 && sizeof(ReduceCall) == 136);
static_assert(sizeof(OpensCall) == 28 && sizeof(ResetDamageCall) == 64&& sizeof(ElementalCall) == 16 && sizeof(ExcessCall) == 32 &&
              sizeof(MaxCall) == 16);

} // namespace ei::vectors::stats
