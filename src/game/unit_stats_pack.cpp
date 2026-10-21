// Resetting creature statistics and packing them into the rounded records the unit carries
// (UnitParams at CUnit + 0x330, UnitVitalsPacked at CUnit + 0x17C). See unit.h.
#include "unit.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <bit>
#include <cstring>

namespace {
constexpr float kZero = 0.0f;                    // 0073B858
constexpr float kByteMax = 255.0f;               // 0073C370
constexpr float kTwo = 2.0f;                     // 0073E02C
constexpr float kSpeedScale = 25.5f;             // 0073E120
constexpr float kOne = 1.0f;
constexpr int kNoNaturalWeapon = 8;              // UnitStats +0xAC after Reset
constexpr int kResetSlotType = 4;
constexpr std::int16_t kResetParams20 = 7;

// FISTP of a float the original first stores (FSTP float, FLD, FISTP).
int RoundStored(float value) { return x87::fistp(value); }

// round(value * 255 / 2), at most 255 (unsigned compare: negative values become 255 too).
std::uint8_t ScaledByte(float value, float scale, float divisor) {
    float scaled = value * scale / divisor;
    auto rounded = static_cast<std::uint32_t>(x87::fistp(scaled));
    if (rounded > 0xFF) rounded = 0xFF;
    return static_cast<std::uint8_t>(rounded);
}

void ResetLayerExceptUnknown20(UnitStatLayer& layer) {
    layer.layerMask = 0;
    for (float& a : layer.absorb) a = kZero;
    layer.state = 0;
    layer.spareA = 0;
    layer.spareB = 0;
}
} // namespace

// --- UnitVitals ------------------------------------------------------------------------------

void* UnitVitals::Reset() {
    hitPoints = kZero;
    maxHitPoints = kZero;
    hitPointRegen = kZero;
    mana = kZero;
    maxMana = kZero;
    manaRegen = kZero;
    regenTicks = 0;
    for (UnitStatPair& pair : protection) {
        pair.current = kZero;
        pair.base = kZero;
    }
    return reinterpret_cast<char*>(this) + sizeof(UnitVitals);
}
EI_REPLACE_METHOD(0x00523370, UnitVitals::Reset);

void* UnitVitals::Pack(UnitVitalsPacked* out) {
    out->hitPoints = static_cast<std::int16_t>(RoundStored(hitPoints));
    out->maxHitPoints = static_cast<std::int16_t>(RoundStored(maxHitPoints));
    out->mana = static_cast<std::int16_t>(RoundStored(mana));
    out->maxMana = static_cast<std::int16_t>(RoundStored(maxMana));
    for (int i = 0; i < 7; ++i) out->protection[i] = static_cast<std::int16_t>(RoundStored(protection[i].current));
    return reinterpret_cast<char*>(out) + sizeof(UnitVitalsPacked);
}

int UnitVitalsPacked::Reset() {
    spare00 = 0;
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

void UnitStats::Pack(CUnit*, UnitParams* params) {
    params->experience = RoundStored(experience);
    float unspent = pendingExperience + experience - spentExperience;
    params->unspentExperience = RoundStored(unspent);
    params->stat10 = static_cast<std::uint8_t>(RoundStored(stat10));
    params->stat18 = static_cast<std::uint8_t>(RoundStored(stat18));
    params->attackRate = static_cast<std::uint8_t>(RoundStored(stat20));
    for (int i = 0; i < 4; ++i) params->modeSpeeds[i] = ScaledByte(modeSpeeds[i], kByteMax, kTwo);
    params->baseSpeed = ScaledByte(speed, kSpeedScale, kOne);
    params->rate = static_cast<std::uint8_t>(RoundStored(rate));
    params->capacity = static_cast<std::int16_t>(RoundStored(capacity));
    params->movementKind = static_cast<std::uint8_t>(movementKind);
    for (int i = 0; i < 5; ++i) params->combat[i] = static_cast<std::uint8_t>(RoundStored(combat[i]));
    params->damage = static_cast<std::uint8_t>(gamecrt::Ftol(damage));         // truncated
    params->damageSpread = static_cast<std::uint8_t>(gamecrt::Ftol(damageSpread));
    params->naturalWeapon = static_cast<std::uint8_t>(naturalWeapon);   // its low byte
    for (int i = 0; i < 6; ++i) {
        params->valuesB0[i] = static_cast<std::uint8_t>(RoundStored(valuesB0[i].current));
        params->valuesE0[i] = static_cast<std::uint8_t>(RoundStored(valuesE0[i].current));
    }
    for (int i = 0; i < 6; ++i) {
        const UnitStatSlot& slot = slots[i];
        UnitParamsSlot& out = params->slots[i];
        out.type = slot.type;
        out.vitalMask = static_cast<std::uint8_t>(slot.vitalMask);
        out.kind = slot.kind;
        out.current = static_cast<std::int16_t>(RoundStored(slot.current));
        out.maximum = static_cast<std::int16_t>(RoundStored(slot.maximum));
        for (int type = 0; type < 7; ++type) {
            double sum = kZero;   // accumulated on the FPU stack
            for (const UnitStatLayer& layer : slot.layers) sum += layer.absorb[type];
            out.protection[type] = static_cast<std::int16_t>(RoundStored(static_cast<float>(sum)));
        }
    }
    std::memcpy(params->attributes, attributes, sizeof(params->attributes));
    std::memcpy(params->skills, skills, sizeof(params->skills));
    std::memcpy(params->skillLevels, skillLevels, sizeof(params->skillLevels));
    params->flags = static_cast<std::uint8_t>(flags);
}
EI_REPLACE_METHOD(0x00522CE0, UnitStats::Pack);

// --- Resets ----------------------------------------------------------------------------------

int UnitStatLayer::Reset() {
    ResetLayerExceptUnknown20(*this);
    return 0;
}

int UnitStatSlot::Reset() {
    type = kResetSlotType;
    vitalMask = 0;
    kind = 0;
    maximum = kZero;
    current = kZero;
    for (UnitStatLayer& layer : layers) ResetLayerExceptUnknown20(layer);
    return 0;
}

int UnitStats::Reset() {
    experience = kZero;
    pendingExperience = kZero;
    spentExperience = kZero;
    stat10Base = stat10 = kZero;
    stat18Base = stat18 = kZero;
    stat20Base = stat20 = kZero;
    speedBase = speed = kZero;
    rateBase = rate = kZero;
    capacity = kZero;
    for (float& value : combat) value = kZero;
    damage = kZero;
    damageSpread = kZero;
    combat[2] = kOne;   // attack spread
    std::memset(damageWeights, 0, sizeof(damageWeights));
    naturalWeapon = kNoNaturalWeapon;
    std::memset(valuesB0, 0, sizeof(valuesB0));
    std::memset(valuesE0, 0, sizeof(valuesE0));
    for (UnitStatSlot& slot : slots) {
        slot.vitalMask = 0;
        slot.kind = 0;
        slot.maximum = kZero;
        slot.current = kZero;
        slot.type = kResetSlotType;
        for (UnitStatLayer& layer : slot.layers) ResetLayerExceptUnknown20(layer);
    }
    std::memset(attributes, 0, sizeof(attributes));
    std::memset(skills, 0, sizeof(skills));
    std::memset(skillLevels, 0, 0x24);   // the last level byte (+0x6F7) is left alone
    flags = 0;
    return 0;
}
EI_REPLACE_METHOD(0x005233E0, UnitStats::Reset);

int UnitStats::ClearOuterLayers() {
    for (UnitStatSlot& slot : slots) {
        for (int i = 1; i < 3; ++i) {
            UnitStatLayer& layer = slot.layers[i];
            layer.layerMask = 0;
            for (float& a : layer.absorb) a = kZero;
            for (float& u : layer.spareAbsorb) u = kZero;
        }
    }
    return 0;
}
