// Creature statistics: attribute, skill and skill level lookups over UnitStats, and small helpers
// of the hit (DamageInfo) and lock records. See unit.h.
#include "game/app/logic_settings.h"
#include "engine/core/random.h"
#include "engine/data/text_manager.h"
#include "game/items/item.h"
#include "game/saves/save_versions.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/rounding.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace {
constexpr float kStatBase = 25.0f;
constexpr float kTwelve = 12.0f;
constexpr float kPercent = 0.01f;
constexpr float kFactorBase = 15.0f;
constexpr float kPointThree = 0.3f;
constexpr float kPointSeven = 0.7f;
constexpr float kPointTwo = 0.2f;
constexpr int kDamageWords = 7;   // words 1..7 of a damage / protection block
constexpr float kPointEight = 0.8f;

// Skill levels 21 and 22 index the level table from these bases.
constexpr int kScaledStrengthSkill = 21;   // skillLevels[21] and [22]: the two skills these read
constexpr int kBackstabSkill = 22;
constexpr int kScaledStrengthLevelBase = 0x3E;
constexpr int kBackstabLevelBase = 0x41;

// The stat range GetFactors divides by (a static initialiser: 35 - 15).
constexpr float kFactorStatRange = /*007AF280*/ 20.0f;

constexpr float kByteMax = 255.0f;
constexpr float kSpeedScale = 25.5f;
constexpr int kResetSlotType = 4;

// Rounded to the nearest integer after a store to float.
int RoundStored(float value) { return lib::RoundEven(value); }

// round(value * 255 / 2), at most 255 (unsigned compare: negative values become 255 too).
std::uint8_t ScaledByte(float value, float scale, float divisor) {
    float scaled = value * scale / divisor;
    auto rounded = static_cast<std::uint32_t>(lib::RoundEven(scaled));
    if (rounded > 0xFF) rounded = 0xFF;
    return static_cast<std::uint8_t>(rounded);
}

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

constexpr float kWorthBase = 1.61f;
constexpr float kTwoDigitsBelow = 4.0f; // a first digit below this keeps two digits
constexpr float kRoundUpTo = 10.0f;     // ... unless the price is at most this
constexpr float kTenth = 0.1f;
constexpr int kKnownLevels = 0x24;      // the skill level bytes summed (UnitStats::skillLevels)

// What a level value is worth (the same curve as RaiseSkill's): the game's settings "Skill Val 1"
// (the scale) and "Skill Val 2" (the divisor), read at the time of the call. The int over the divisor rounds once.
float Worth(int value) {
    const LogicSettings& settings = g_logicSettings;
    const float exponent = value / settings.skillVal[1];
    return lib::Pow(kWorthBase, exponent) * settings.skillVal[0];
}

constexpr int kNoSlot = 7;              // an entry of the coverage table that covers nothing
constexpr int kBodySlot = 1;
constexpr int kLayerThere = 2;          // UnitStatLayer::state from which a layer counts
constexpr int kLayerAdded = 3;
constexpr int kQuickItems = 4;
constexpr int kArmourSlots = 7;         // Unit::armor

// For each armour type, the three body slots it covers (kNoSlot: none).
constexpr int kArmourCoverage[7][3] = /*0073E048*/ {{0, 7, 7}, {1, 2, 3}, {7, 4, 5}, {1, 2, 3}, {7, 4, 5}, {7, 4, 5}, {7, 2, 3}};

int CoveredSlot(ArmorType type, int which) { return kArmourCoverage[static_cast<int>(type)][which]; }

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
    // The first letter of the skill's first stat name picks the stat; none without a record.
    const SkillRecord* skillRecord = unitdb::SkillAt(index);
    if (!skillRecord || skillRecord->statNames.empty()) return skill;
    switch (skillRecord->statNames.front()[0]) {
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
    // Levels compared signed; a lock of level 0 gives the key's level + 1, so a key of level -1 (0xFFFFFFFF) no longer
    // opens it (Rebirth's FixLeversActivate; the original compared unsigned).
    if (lock->level == 0) return static_cast<int>(level + 1);
    return static_cast<std::int32_t>(level) < static_cast<std::int32_t>(lock->level) ? 0 : 1;
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
    if (info->Weight(DamageType::Fire) == 0.0f && info->Weight(DamageType::Acid) == 0.0f &&
        info->Weight(DamageType::Lightning) == 0.0f) {
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

// UnitStats::Load / Save: the statistics block in saved games (the
// server controller's copy, combat_save.cpp; the party's records, party.cpp). See unit.h.

void UnitStats::Load(ByteStream* stream) {
    pendingExperience = 0.0f;
    int version = stream->version;
    unsigned size;
    if (version < kVersionStatsPastFlags) size = offsetof(UnitStats, flags);
    else if (version < kVersionFixedMaxHitPoints) size = offsetof(UnitStats, fixedMaxHitPoints);
    else if (version < kVersionPendingExperience) size = offsetof(UnitStats, pendingExperience);
    else size = sizeof(UnitStats);
    stream->Read(this, size);
}

void UnitStats::Save(ByteStream* stream) const { stream->Write(this, sizeof(UnitStats)); }

// --- Pack --------------------------------------------------------------------------------------------

// Resetting creature statistics and packing them into the rounded records the unit carries (UnitParams and
// UnitVitalsPacked). See unit.h.

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

void UnitStats::Reset() {
    *this = {};
    combat[CombatIndex::AttackSpread] = 1.0f;
    naturalWeapon = WeaponType::NoWeapon;   // after Reset
    for (UnitStatSlot& slot : slots) slot.type = kResetSlotType;
}

void UnitStats::ClearOuterLayers() {
    for (UnitStatSlot& slot : slots) {
        for (int i = 1; i < 3; ++i) {
            UnitStatLayer& layer = slot.layers[i];
            layer.layerMask = 0;
            for (float& a : layer.absorb) a = 0.0f;
            for (float& u : layer.spareAbsorb) u = 0.0f;
        }
    }
}

// --- Unit hit location -------------------------------------------------------------------------------

// Where an attack lands when the body part it aims at is gone. See unit.h
// (UnitStats::PickLocation) and docs/notes/units.md ("Statistics in play").

int UnitStats::PickLocation(int location) {
    if (static_cast<unsigned>(location) < kSlots) {
        if (slots[location].kind == kWorking) return location;
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

// --- Skill price ---------------------------------------------------------------------------------------

// What learning a skill level or raising a school costs; the interface's skill pages call them. See
// docs/notes/units.md ("Statistics in play").

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
    float price = lib::Pow(g_logicSettings.perkPowerBase, static_cast<float>(known)) * cost;

    // The power of ten of the first digit.
    float power = lib::Pow(10.0f, std::floor(lib::Log10(price)));
    float leading = price / power;
    if (leading >= kTwoDigitsBelow) return lib::Truncate(std::floor(leading + 0.5f) * power);
    // Below 4, or NaN: a price of 0 gives power 0 and 0 / 0, which this branch returns as 0 (it
    // occurs; the plain leading < 4 would return the NaN's integer conversion).
    if (power <= kRoundUpTo) return lib::Truncate(std::ceil(price));
    power *= kTenth;
    leading = price / power;
    return lib::Truncate(std::floor(leading + 0.5f) * power);
}

// --- Armour layers -----------------------------------------------------------------------------

// Creature statistics: the armour layers of the body slots, the body's packed protection and two skill values
// (an attribute with the quick items' bonuses, a spell school's skill). See unit.h (UnitStats) and
// docs/notes/units.md ("Statistics in play").

void UnitStats::AddArmourLayer(ItemArmor* piece) {
    for (int which = 0; which < 3; ++which) {
        int slotIndex = CoveredSlot(piece->GetType(), which);
        if (slotIndex == kNoSlot) continue;
        UnitStatSlot& slot = slots[slotIndex];
        UnitStatLayer& layer = slot.layers[piece->GetTypeGroup()];
        if (slot.kind == 0) continue;
        int part = which != 0 ? 1 : 0;
        for (int k = 0; k < 7; ++k) layer.absorb[k] = layer.absorb[k] + piece->absorb[part][k];
        layer.state = kLayerAdded;
        layer.layerMask |= static_cast<int>(piece->layerMask[part]);
    }
}

void UnitStats::RebuildArmourLayers(Unit* unit) {
    for (UnitStatSlot& slot : slots) std::memset(&slot.layers[1], 0, 2 * sizeof(UnitStatLayer));
    for (int i = 0; i < kArmourSlots; ++i) {
        ItemArmor* piece = unit->armor[i];
        if (!piece) continue;
        float condition = piece->condition;
        if (lib::RoundEven(condition) > 0) AddArmourLayer(piece);   // to nearest
    }
}

void UnitStats::PackBodyProtection(Unit* unit, const UnitVitals* vitals, DamageWords* out) {
    bool keyed = IsTextKey(unit->id);
    int top = keyed ? 2 : 0;
    const UnitStatSlot& body = slots[kBodySlot];
    for (int k = 0; k < 7; ++k) {
        float value = vitals->protection[k].current;
        for (int j = top; j >= 0; --j) {
            if (body.layers[j].state >= kLayerThere) value += body.layers[j].absorb[k];
        }
        out->words[1 + k] = static_cast<std::uint16_t>(lib::RoundEven(value));
    }
}

float UnitStats::AttributeWithItems(int index, Unit* unit) {
    // The effects' Science and Taming; the second attribute is read by nothing.
    const float effect = index == 0 ? EffectTotal(unit, EffectStat::Science)
                         : index == 2 ? EffectTotal(unit, EffectStat::Taming)
                                      : 0.0f;
    if (!IsTextKey(unit->id)) return attributes[index] + effect;    // any index
    float best = 0.0f;
    for (std::uint32_t i = 0; i < kQuickItems; ++i) {
        Item* item = unit->GetQuickItem(i);
        if (!item) continue;
        const auto* prototype = itemdb::RecordAs<itemdb::QuickItemRecord>(item->GetPrototype());
        float bonus = static_cast<float>(prototype->attributeBonus[index]);   // any index
        best = MaxKeepB(best, bonus);   // a select
    }
    switch (index) {
    case 0: return (dexterity - kStatBase) + (attributes[0] + effect) + best;
    case 1: return (dexterity - kStatBase) + attributes[1] + best;
    case 2: return (intelligence - kStatBase) + (attributes[2] + effect) + best;
    default: return 0.0f + best;
    }
}

float UnitStats::SpellSchoolSkill(SpellSchool spellSchool, Unit* unit) {
    int school = static_cast<int>(spellSchool);
    if (school == 4) school = 3;
    else if (school == 6 || school == 7) school = 5;
    float base = 0.0f;
    if (school >= 0 && school <= 2) base = skills[3] + EffectTotal(unit, EffectStat::ElementalSkill);
    else if (school >= 3 && school <= 4) base = skills[4] + EffectTotal(unit, EffectStat::SenseSkill);
    else if (school >= 5 && school <= 7) base = skills[5] + EffectTotal(unit, EffectStat::AstralSkill);
    // skillLevels[school + 7] and its record.
    float level = SkillLevelValue(school + 7);

    return (base + level) + (intelligence - kStatBase);
}
