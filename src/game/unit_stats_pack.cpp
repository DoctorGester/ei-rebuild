// Resetting creature statistics and packing them into the rounded records the unit carries (UnitParams and
// UnitVitalsPacked). See unit.h.
#include "unit.h"

#include "lib/rounding.h"

#include <bit>
#include <cstdint>
#include <cstring>

namespace {
constexpr float kByteMax = 255.0f;
constexpr float kSpeedScale = 25.5f;
constexpr int kNoNaturalWeapon = 8;              // after Reset
constexpr int kResetSlotType = 4;
constexpr std::int16_t kResetParams20 = 7;

// Rounded to the nearest integer after a store to float.
int RoundStored(float value) { return lib::RoundEven(value); }

// Truncated as _ftol does: the low half of the 64-bit integer (0 for a NaN).
int Truncate(float value) { return static_cast<int>(static_cast<std::int64_t>(value)); }

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

void UnitVitals::Reset() {
    hitPoints = 0.0f;
    maxHitPoints = 0.0f;
    hitPointRegen = 0.0f;
    mana = 0.0f;
    maxMana = 0.0f;
    manaRegen = 0.0f;
    regenTicks = 0;
    for (UnitStatPair& pair : protection) {
        pair.current = 0.0f;
        pair.base = 0.0f;
    }
}

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
    params->damage = static_cast<std::uint8_t>(Truncate(damage));
    params->damageSpread = static_cast<std::uint8_t>(Truncate(damageSpread));
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
