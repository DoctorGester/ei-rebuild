// Creature statistics: attribute, skill and skill level lookups over UnitStats, and small helpers
// of the hit (DamageInfo) and lock records. See unit.h.
#include "logic_settings.h"

#include "random.h"
#include "save_versions.h"
#include "spell.h"
#include "unit.h"
#include "unit_db.h"
#include "ei/rebirth.h"
#include "lib/nan_audit.h"
#include "lib/rounding.h"

#include <bit>
#include <cstdint>
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

} // namespace

float UnitStats::StrengthRatio() { return strength / g_logicSettings.hpVal[0]; }

float UnitStats::DexterityRatio() { return dexterity / g_logicSettings.mpVal[0]; }

float UnitStats::Period20() { return kStatBase / intelligence; }

float UnitStats::AttributeBase(int index) { return static_cast<float>(attributes[index]); }

float UnitStats::AttributeWithBonus(int index) {
    switch (index) {
    case 0: return (dexterity - kStatBase) + static_cast<float>(attributes[0]);
    case 1: return (dexterity - kStatBase) + static_cast<float>(attributes[1]);
    case 2: return (intelligence - kStatBase) + static_cast<float>(attributes[2]);
    default: return 0.0f;
    }
}

float UnitStats::SkillBase(int index) { return static_cast<float>(skills[index]); }

float UnitStats::SkillWithBonus(std::uint32_t index) {
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

float UnitStats::SkillLevelValue(int index) {
    std::uint8_t level = skillLevels[index];
    if (level != 0) {
        const ::SkillLevelRecord* record = unitdb::SkillLevelAt(static_cast<std::uint32_t>(index * 3 - 1 + level));
        if (record) return record->Value();
    }
    return 0.0f;
}

float UnitStats::ScaledStrength() {
    float scaledStrength = strength * kTwelve;
    std::uint8_t level = skillLevels[kScaledStrengthSkill];
    if (level != 0) {
        const ::SkillLevelRecord* record = unitdb::SkillLevelAt(level + kScaledStrengthLevelBase);
        if (record) {
            return (record->Value() * kPercent + 1.0f) * scaledStrength;
        }
    }
    return (0.0f * kPercent + 1.0f) * scaledStrength;
}

float UnitStats::BackstabFactor() {
    float base = g_logicSettings.backstabAdd;
    std::uint8_t level = skillLevels[kBackstabSkill];
    if (level != 0) {
        const ::SkillLevelRecord* record = unitdb::SkillLevelAt(level + kBackstabLevelBase);
        if (record) {
            return record->Value() * kPercent + base;
        }
    }
    return 0.0f * kPercent + base;
}

float UnitStats::Reduce(float value) {
    float taken = 0.0f;
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind < 1) continue;
        if (slot.current >= 0.0f) {
            taken = (slot.maximum - slot.current) / slot.maximum * slot.factor * value + taken;
        } else {
            taken = value * slot.factor + taken;   // also NaN
        }
    }
    return value - taken;
}

float UnitStats::ReduceKind1(float value) {
    float taken = 0.0f;
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind == 1) taken = value * slot.factor + taken;
    }
    return value - taken;
}

void UnitStats::GetFactors(float* scaleX, float* scaleY, int) {
    float x = (strength - kFactorBase) / kFactorStatRange;
    float y = 1.0f - (dexterity - kFactorBase) / kFactorStatRange;
    *scaleX = y * kPointThree + x * kPointSeven;
    *scaleY = (y * kPointEight + x * kPointTwo) * kPointEight + kPointTwo;
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

bool HasElementalDamage(const DamageInfo* info) {
    EI_NAN_AUDIT(info->typeWeights[3], info->typeWeights[4], info->typeWeights[5]);
    if (info->typeWeights[3] == 0.0f && info->typeWeights[4] == 0.0f && info->typeWeights[5] == 0.0f) {
        return false;
    }
    return true;
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

// --- Io ----------------------------------------------------------------------------------------------

// UnitStats::LoadFrom / SaveTo: the statistics block in saved games (the
// server controller's copy, combat_save.cpp; the party's records, party_save.cpp). See unit.h.

namespace {
} // namespace

int UnitStats::LoadFrom(ByteStream* stream) {
    pendingExperience = 0.0f;
    int version = stream->version;
    unsigned size;
    if (version < kVersionStatsPastFlags) size = offsetof(UnitStats, flags);
    else if (version < kVersionFixedMaxHitPoints) size = offsetof(UnitStats, fixedMaxHitPoints);
    else if (version < kVersionPendingExperience) size = offsetof(UnitStats, pendingExperience);
    else size = sizeof(UnitStats);
    return stream->Read(this, size);
}

void UnitStats::SaveTo(ByteStream* stream) { stream->Write(this, sizeof(UnitStats)); }

// --- Pack --------------------------------------------------------------------------------------------

// Resetting creature statistics and packing them into the rounded records the unit carries (UnitParams and
// UnitVitalsPacked). See unit.h.

namespace {
constexpr float kByteMax = 255.0f;
constexpr float kSpeedScale = 25.5f;
constexpr int kNoNaturalWeapon = 8;              // after Reset
constexpr int kResetSlotType = 4;
constexpr std::int16_t kResetParams20 = 7;

// Rounded to the nearest integer after a store to float.
int RoundStored(float value) { return lib::RoundEven(value); }

// round(value * 255 / 2), at most 255 (unsigned compare: negative values become 255 too).
std::uint8_t ScaledByte(float value, float scale, float divisor) {
    float scaled = value * scale / divisor;
    auto rounded = static_cast<std::uint32_t>(lib::RoundEven(scaled));
    if (rounded > 0xFF) rounded = 0xFF;
    return static_cast<std::uint8_t>(rounded);
}

void ResetLayerKeepingSpareAbsorb(UnitStatLayer& layer) {
    layer.layerMask = 0;
    for (float& a : layer.absorb) a = 0.0f;
    layer.state = 0;
    layer.reserved[0] = 0;
    layer.reserved[1] = 0;
}
} // namespace

// --- UnitVitals ------------------------------------------------------------------------------

void UnitVitals::Pack(UnitVitalsPacked* out) {
    out->hitPoints = static_cast<std::int16_t>(RoundStored(hitPoints));
    out->maxHitPoints = static_cast<std::int16_t>(RoundStored(maxHitPoints));
    out->mana = static_cast<std::int16_t>(RoundStored(mana));
    out->maxMana = static_cast<std::int16_t>(RoundStored(maxMana));
    for (int i = 0; i < 7; ++i) out->protection[i] = static_cast<std::int16_t>(RoundStored(protection[i].current));
}

int UnitVitalsPacked::Reset() {
    unused_00[0] = 0;
    hitPoints = 0;
    maxHitPoints = 0;
    mana = 0;
    maxMana = 0;
    for (std::int16_t& p : protection) p = 0;
    return 0;
}

// --- UnitParams ------------------------------------------------------------------------------

int UnitParams::Reset() {
    std::memset(this, 0, sizeof(UnitParams));
    lastHitSource = kResetParams20;
    for (UnitParamsSlot& slot : slots) slot.type = kResetSlotType;
    return 0;
}

void UnitStats::Pack(UnitParams* params) {
    params->experience = RoundStored(experience);
    float unspent = pendingExperience + experience - spentExperience;
    params->unspentExperience = RoundStored(unspent);
    params->strength = static_cast<std::uint8_t>(RoundStored(strength));
    params->dexterity = static_cast<std::uint8_t>(RoundStored(dexterity));
    params->intelligence = static_cast<std::uint8_t>(RoundStored(intelligence));
    for (int i = 0; i < 4; ++i) params->modeSpeeds[i] = ScaledByte(modeSpeeds[i], kByteMax, 2.0f);
    params->baseSpeed = ScaledByte(speed, kSpeedScale, 1.0f);
    params->rate = static_cast<std::uint8_t>(RoundStored(rate));
    params->capacity = static_cast<std::int16_t>(RoundStored(capacity));
    params->movementKind = static_cast<std::uint8_t>(movementKind);
    for (int i = 0; i < 5; ++i) params->combat[i] = static_cast<std::uint8_t>(RoundStored(combat[i]));
    params->damage = static_cast<std::uint8_t>(lib::Truncate(damage));
    params->damageSpread = static_cast<std::uint8_t>(lib::Truncate(damageSpread));
    params->naturalWeapon = static_cast<std::uint8_t>(naturalWeapon);   // its low byte
    for (int i = 0; i < 6; ++i) {
        params->perception[i] = static_cast<std::uint8_t>(RoundStored(perception[i].current));
        params->conspicuity[i] = static_cast<std::uint8_t>(RoundStored(conspicuity[i].current));
    }
    for (int i = 0; i < 6; ++i) {
        const UnitStatSlot& slot = slots[i];
        UnitParamsSlot& out = params->slots[i];
        out.type = slot.type;
        out.vitalMask = static_cast<std::uint8_t>(slot.vitalMask);
        out.kind = slot.kind;
        out.current = static_cast<std::int16_t>(RoundStored(slot.current));
        out.maximum = static_cast<std::uint16_t>(RoundStored(slot.maximum));
        for (int type = 0; type < 7; ++type) {
            float sum = 0.0f;
            for (const UnitStatLayer& layer : slot.layers) sum += layer.absorb[type];
            out.protection[type] = static_cast<std::int16_t>(RoundStored(sum));

        }
    }
    std::memcpy(params->attributes, attributes, sizeof(params->attributes));
    std::memcpy(params->skills, skills, sizeof(params->skills));
    std::memcpy(params->skillLevels, skillLevels, sizeof(params->skillLevels));
    params->flags = static_cast<std::uint8_t>(flags);
}

// --- Resets ----------------------------------------------------------------------------------

int UnitStats::Reset() {
    experience = 0.0f;
    pendingExperience = 0.0f;
    spentExperience = 0.0f;
    strengthBase = strength = 0.0f;
    dexterityBase = dexterity = 0.0f;
    intelligenceBase = intelligence = 0.0f;
    speedBase = speed = 0.0f;
    rateBase = rate = 0.0f;
    capacity = 0.0f;
    for (float& value : combat) value = 0.0f;
    damage = 0.0f;
    damageSpread = 0.0f;
    combat[2] = 1.0f;   // attack spread
    std::memset(damageWeights, 0, sizeof(damageWeights));
    naturalWeapon = kNoNaturalWeapon;
    std::memset(perception, 0, sizeof(perception));
    std::memset(conspicuity, 0, sizeof(conspicuity));
    for (UnitStatSlot& slot : slots) {
        slot.vitalMask = 0;
        slot.kind = 0;
        slot.maximum = 0.0f;
        slot.current = 0.0f;
        slot.type = kResetSlotType;
        for (UnitStatLayer& layer : slot.layers) ResetLayerKeepingSpareAbsorb(layer);
    }
    std::memset(attributes, 0, sizeof(attributes));
    std::memset(skills, 0, sizeof(skills));
    std::memset(skillLevels, 0, 0x24);   // the last level byte is left alone
    flags = 0;
    return 0;
}

int UnitStats::ClearOuterLayers() {
    for (UnitStatSlot& slot : slots) {
        for (int i = 1; i < 3; ++i) {
            UnitStatLayer& layer = slot.layers[i];
            layer.layerMask = 0;
            for (float& a : layer.absorb) a = 0.0f;
            for (float& u : layer.spareAbsorb) u = 0.0f;
        }
    }
    return 0;
}

// --- Unit hit location -------------------------------------------------------------------------------

// Where an attack lands when the body part it aims at is gone. See unit.h
// (UnitStats::PickLocation) and docs/notes/units.md ("Statistics in play").

namespace {
constexpr int kWorking = 3;       // UnitStatSlot::kind
constexpr int kSlots = 6;
constexpr int kFallback = 1;      // the body

// The neighbours tried, in order, for each location (-1 ends a row).
constexpr signed char kNeighbours[kSlots][kSlots] = {
    {1, 2, 3, -1},          // head
    {0, 2, 3, 4, 5, -1},    // body
    {3, 1, -1},             // hands
    {2, 1, -1},
    {5, 1, -1},             // legs
    {4, 1, -1},
};
} // namespace

int UnitStats::PickLocation(int location) {
    // The slots are indexed with any location (only the neighbour table is guarded).
    if (slots[location].kind == kWorking) return location;
    if (static_cast<unsigned>(location) < kSlots) {
        for (const signed char* n = kNeighbours[location]; *n >= 0; ++n)
            if (slots[*n].kind == kWorking) return *n;
    }
    int working[kSlots];
    unsigned count = 0;
    for (int i = 0; i < kSlots; ++i)
        if (slots[i].kind == kWorking) working[count++] = i;
    if (count == 0) return kFallback;
    return working[g_random.Next() % count];   // the draw is inlined
}
