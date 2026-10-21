// Creature statistics that change while the game runs: the recomputation after hits, heals and
// item changes (UnitStats::Recompute), regeneration every world update (UnitVitals::Regenerate),
// the body slots' healing, the experience curves and experience gain. See unit.h and
// docs/notes/units.md ("Statistics in play").
#include "map_object.h"
#include "unit.h"

#include "object_tables.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "originals_units.h"
#include "logic_settings.h"
#include "lib/crt.h"
#include "ei/x87.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstring>

// The formulas must keep the original's order of operations (see unit_stats.cpp). Values the
// original keeps on the FPU stack are double locals; values it stores to a float are float
// locals.

namespace {
constexpr float kZero = 0.0f;                                       // 0073B858
constexpr float kOne = 1.0f;                                        // 0073B84C
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);       // 0073C548 (0.01)
constexpr float kPercentB = std::bit_cast<float>(0x3C23D70Au);      // 0073D09C (0.01, a second copy)
constexpr float kHitPointEffect = std::bit_cast<float>(0x3BA3D70Au);  // 0073E130 (0.005)
constexpr float kDamageEffect = std::bit_cast<float>(0x3B449BA6u);  // 0073E134 (0.003)
constexpr float kTwelve = 12.0f;                                    // 0073E138
constexpr float kPointTwo = std::bit_cast<float>(0x3E4CCCCDu);      // 0073BAD4 (0.2)
constexpr float kTen = 10.0f;                                       // 0073BAAC
constexpr float kRevived = std::bit_cast<float>(0x3A83126Fu);       // 0073CCF4 (0.001)
constexpr double kMinHeal = 0.001;                                  // 0073BA88 (a double)
constexpr std::uint32_t kNoCapacity = 0x49742400u;                  // 1000000.0f
constexpr std::uint32_t kKeyedUnknown044 = 0xC0000000u;             // -2.0f
constexpr float kUnkeyedRateBase = 15.0f;                           // 0x41700000

constexpr int kRegenPeriod = 15;
constexpr int kClassServerObject = 0x39;
constexpr int kSlotArms = 2;
constexpr int kSlotLegs = 3;

// Effect types read here (MapEffect::type).
constexpr std::uint32_t kEffectStrengthen = 0x1D;
constexpr std::uint32_t kEffectWeaken = 0x1E;
constexpr std::uint32_t kEffectRegeneration = 0x1F;
constexpr int kEffectTypes = 0x28;

// Skills whose level records (unitdb::SkillLevels, index skill * 3 - 1 + level) feed the formulas.
constexpr int kSkillBodyB0 = 15;      // raises valuesB0[1] by its percentage
constexpr int kSkillHitPoints = 16;
constexpr int kSkillMana = 17;
constexpr int kSkillHitPointRegen = 18;
constexpr int kSkillManaRegen = 19;
constexpr int kSkillRate = 20;
constexpr int kSkillCapacity = 21;
constexpr int kSkillStat10 = 23;
constexpr int kSkillStat18 = 24;
constexpr int kSkillStat20 = 25;

// base ^ (ln(experience / divisor) / ln(logBase)) exactly as the original computes it: FYL2X with
// FLDLN2 for both logarithms and the CRT's _CIpow (006E81A0, ST1 ^ ST0). Returned unrounded.
double ExperienceCurve(const float* base, const float* experience, const float* divisor, const float* logBase) {
    return libcrt::CiPow(*base, x87::flog(static_cast<double>(*experience) / *divisor) / x87::flog(*logBase));
}

// 1.04 (the double at 0073E128) ^ (intelligence - 25) through _CIpow, unrounded.
double IntelligenceFactor(const float* intelligence) {
    return libcrt::CiPow(1.04 /*0073E128*/,
                         static_cast<double>(*intelligence) - 25.0f /*0073E124*/);
}

// The level value of skill `skill` (0 without a level or a record): the record's +0x34, loaded
// like the original's FILD of a zero-extended qword (exact).
double LevelValueOf(const UnitStats* stats, int skill) {
    std::uint8_t level = stats->skillLevels[skill];
    if (level == 0) return kZero;
    auto* record = reinterpret_cast<const SkillLevelRecord*>(
        unitdb::SkillLevels().RecordAt(static_cast<std::uint32_t>(skill * 3 - 1 + level), unitdb::kSkillLevelSize));
    if (!record) return kZero;
    return record->Value();
}

// The same for a level record pointer from UnitStats::SkillLevelRecord.
double RecordValue(const char* record) { return reinterpret_cast<const SkillLevelRecord*>(record)->Value(); }

// The original's branch-free selects (FCOMP, the C0 bit as a mask): `a` when a < b or unordered.
float SelectLess(float a, float b, float ifLess, float otherwise) { return a >= b ? otherwise : ifLess; }

MapEffect EffectAt(const CUnit* unit, int index) { return unit->effects[index]; }
float EffectValueField(const MapEffect& effect) {
    float value;
    std::memcpy(&value, &effect.value, sizeof(value));
    return value;
}

CEngineObject* ServerObjectOf(const CUnit* unit) { return unit->companion; }
UnitVitals* VitalsOf(CEngineObject* server) { return &static_cast<CMapObjectServer*>(server)->vitals; }

// strengthen / weaken scale: (1 + v(0x1D) * k) / (1 + v(0x1E) * k), the numerator stored as a
// float; 1 without a unit. Unrounded.
double EffectScale(CUnit* unit, float k);
} // namespace

namespace {
double EffectScale(CUnit* unit, float k) {
    if (!unit) return kOne;
    float strengthen = static_cast<float>(unit->EffectValue(kEffectStrengthen) * k + kOne);
    return strengthen / (unit->EffectValue(kEffectWeaken) * k + kOne);
}

// value scaled by EffectScale, at least 1 (NaN kept).
float ScaledAtLeastOne(CUnit* unit, float k, float value) {
    double scale = EffectScale(unit, k);
    float scaled = static_cast<float>(scale * value);
    float one = kOne;
    float s = scaled;
    return SelectLess(one, s, s, one);
}

// The lowest current / maximum ratio of the used slots of `type` that are not exactly full.
float LowestSlotRatio(const UnitStats* stats, int type) {
    float lowest = kOne;
    for (const UnitStatSlot& slot : stats->slots) {
        if (slot.kind == 0 || slot.type != type) continue;
        EI_NAN_AUDIT(slot.current, slot.maximum);
        if (slot.current == slot.maximum) continue;
        float ratio = slot.current / slot.maximum;
        float r = ratio;
        lowest = SelectLess(r, lowest, r, lowest);
    }
    return lowest;
}

float RatioFactor(float lowest) {
    EI_NAN_AUDIT(lowest);
    if (lowest <= GameLogicSettings().damageLevel2) return GameLogicSettings().damageLevel2Value;
    if (lowest <= GameLogicSettings().damageLevel1) return GameLogicSettings().damageLevel1Value;
    return kOne;
}
} // namespace

// --- Regeneration ----------------------------------------------------------------------------

bool UnitVitals::Regenerate(UnitStats* stats, CUnit* unit, char inCombat) {
    int ticks = ++regenTicks;
    if (ticks < kRegenPeriod && ticks > 0) return false;
    bool changed = false;
    regenTicks = 0;
    EI_NAN_AUDIT(mana, maxMana);
    if (inCombat == 0 && mana < maxMana) {
        double regen = manaRegen;
        float raised = static_cast<float>(
            (LevelValueOf(stats, kSkillManaRegen) * kPercent + kOne) * regen * maxMana + mana);
        float limit = maxMana;
        float r = raised;
        mana = SelectLess(limit, r, limit, r);
        changed = true;
    }
    EI_NAN_AUDIT(hitPoints, maxHitPoints);
    if (hitPoints < maxHitPoints) {
        double regen = hitPointRegen;
        float rate = static_cast<float>((LevelValueOf(stats, kSkillHitPointRegen) * kPercent + kOne) * regen);
        if (unit) {
            double boost = unit->EffectValue(kEffectRegeneration);
            if (boost > kZero) rate = static_cast<float>(boost * rate);
        }
        float scale = maxHitPoints;
        stats->HealSlots(scale, static_cast<float>(scale * rate));
        hitPoints = static_cast<float>(stats->Reduce(maxHitPoints));
        changed = true;
    }
    return changed;
}
EI_REPLACE_METHOD(0x00523740, UnitVitals::Regenerate);

// --- Body slots ------------------------------------------------------------------------------

bool UnitStats::HealSlots(float scale, float amount) {
    float magnitude = std::bit_cast<float>(std::bit_cast<std::uint32_t>(amount) & 0x7FFFFFFFu);
    // Kept as the original's test: a NaN amount heals nothing. It occurs in every run: LearnSkill's
    // heal (the default NaN) while a new unit's stats are being set up (C16f's NaN audit).
    if (!(magnitude > kMinHeal)) return true;
    double total = kZero;   // accumulated on the FPU stack
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind > 1) EI_NAN_AUDIT(slot.current, slot.maximum);
        if (slot.kind > 1 && slot.current < slot.maximum) total += slot.factor;
    }
    EI_NAN_AUDIT(total);
    if (total == kZero) return true;
    float share = static_cast<float>(amount / (total * scale));
    for (UnitStatSlot& slot : slots) {
        if (slot.kind <= 1 || slot.current >= slot.maximum) continue;
        float limit = slot.maximum;
        float raised = static_cast<float>(share * limit + slot.current);
        float r = raised;
        float healed = SelectLess(limit, r, limit, r);
        slot.current = healed;
        if (healed >= kRevived) slot.kind = 3;
    }
    return true;
}
EI_REPLACE_METHOD(0x00525800, UnitStats::HealSlots);

BOOL UnitStats::Heal(UnitVitals* vitals, float amount) {
    HealSlots(vitals->maxHitPoints, amount);
    vitals->hitPoints = static_cast<float>(Reduce(vitals->maxHitPoints));
    return 1;
}

BOOL UnitStats::RegenerateSlots(UnitVitals* vitals) {
    for (UnitStatSlot& slot : slots) {
        if (slot.kind != 3) continue;
        float limit = slot.maximum;
        float raised = static_cast<float>(vitals->hitPointRegen * limit + slot.current);
        float r = raised;
        slot.current = SelectLess(r, limit, r, limit);
    }
    vitals->hitPoints = static_cast<float>(Reduce(vitals->maxHitPoints));
    return 1;
}

void UnitStats::SetSlotMaxima(float maxHitPoints) {
    float ratio = maxHitPoints / slots[1].share;
    for (UnitStatSlot& slot : slots) {
        if (slot.kind <= 0) continue;
        float limit = ratio * slot.share;
        slot.maximum = limit;
        float m = limit;
        float current = slot.current;
        slot.current = SelectLess(m, current, m, current);
    }
}

void UnitStats::ScaleSlotMaxima(float maxHitPoints) {
    float ratio = maxHitPoints / slots[1].share;
    for (UnitStatSlot& slot : slots) {
        if (slot.kind <= 0) continue;
        double limit = ratio * slot.share;   // kept on the FPU stack for the proportion
        float factor = static_cast<float>(limit / slot.maximum);
        slot.maximum = static_cast<float>(limit);
        float m = static_cast<float>(limit);
        float scaled = factor * slot.current;
        float s = scaled;
        slot.current = SelectLess(m, s, m, s);
    }
}

void* UnitStats::RefillSlots() {
    for (UnitStatSlot& slot : slots) {
        if (slot.kind >= 2) std::memcpy(&slot.current, &slot.maximum, sizeof(float));   // dword copy
    }
    UnitStatSlot* end = slots + 6;
    return &end->maximum;   // where the original's loop pointer stops
}
EI_REPLACE_METHOD(0x00525AF0, UnitStats::RefillSlots);

void* UnitStats::RestoreSlots() {
    for (UnitStatSlot& slot : slots) {
        if (slot.kind > 0) {
            slot.kind = 3;
            std::memcpy(&slot.current, &slot.maximum, sizeof(float));
        }
    }
    UnitStatSlot* end = slots + 6;
    return &end->kind;   // where the original's loop pointer stops
}
EI_REPLACE_METHOD(0x00523C80, UnitStats::RestoreSlots);

float UnitStats::LegsFactor(int) { return RatioFactor(LowestSlotRatio(this, kSlotLegs)); }

float UnitStats::ArmsFactor(int) { return RatioFactor(LowestSlotRatio(this, kSlotArms)); }

int UnitStats::MovementState(char checkLoad) {
    float lowest = LowestSlotRatio(this, kSlotLegs);
    if (checkLoad != 0 && load > capacity) return 2;
    EI_NAN_AUDIT(lowest);
    if (lowest < GameLogicSettings().damageLevelRunLimit) return 1;
    return 0;
}

// --- Maxima ----------------------------------------------------------------------------------

double UnitStats::HitPointCurve() {
    double curve = ExperienceCurve(&GameLogicSettings().hpVal[3], &experience, &GameLogicSettings().hpVal[2], &GameLogicSettings().hpVal[4]);
    double scaled = curve * (stat10 / GameLogicSettings().hpVal[0] * GameLogicSettings().hpVal[1]);
    return (LevelValueOf(this, kSkillHitPoints) * kPercent + kOne) * scaled;
}
EI_REPLACE_METHOD(0x00523E80, UnitStats::HitPointCurve);

double UnitStats::ManaCurve() {
    double curve =
        ExperienceCurve(&GameLogicSettings().mpVal[3], &experience, &GameLogicSettings().mpVal[2], &GameLogicSettings().mpVal[4]);
    double scaled = curve * (stat18 / GameLogicSettings().mpVal[0] * GameLogicSettings().mpVal[1]);
    return (LevelValueOf(this, kSkillMana) * kPercent + kOne) * scaled;
}
EI_REPLACE_METHOD(0x00523F50, UnitStats::ManaCurve);

double UnitStats::RateValue() {
    float base = stat18 * kPointTwo + kTen;
    float level = static_cast<float>(LevelValueOf(this, kSkillRate));   // FILD, FSTP float
    float arms = ArmsFactor(1);
    return (level * kPercentB + kOne) * arms * base;
}
EI_REPLACE_METHOD(0x00524320, UnitStats::RateValue);

float __stdcall ScaleDamageByEffects(CUnit* unit, float value) { return ScaledAtLeastOne(unit, kDamageEffect, value); }

double __stdcall HitPointEffectScale(CUnit* unit) { return EffectScale(unit, kHitPointEffect); }
EI_REPLACE(0x00523DB0, HitPointEffectScale);

float __stdcall ScaleHitPointsByEffects(CUnit* unit, float value) {
    return ScaledAtLeastOne(unit, kHitPointEffect, value);
}
EI_REPLACE(0x00523E00, ScaleHitPointsByEffects);

void UnitStats::RecomputeHitPoints(UnitVitals* vitals, CUnit* unit) {
    if (IsTextKey(unit->id_) & 0xFF) {
        vitals->maxHitPoints = static_cast<float>(HitPointCurve());
    } else {
        std::memcpy(&vitals->maxHitPoints, &fixedMaxHitPoints, sizeof(float));
    }
    float limit = ScaledAtLeastOne(unit, kHitPointEffect, vitals->maxHitPoints);
    vitals->maxHitPoints = limit;
    ScaleSlotMaxima(limit);
    double hitPoints = Reduce(vitals->maxHitPoints);
    vitals->hitPoints = static_cast<float>(hitPoints);
    if (hitPoints > kZero) flags |= 8;
}
EI_REPLACE_METHOD(0x005243D0, UnitStats::RecomputeHitPoints);

void UnitStats::Recompute(UnitVitals* vitals, CUnit* unit) {
    bool keyed = (IsTextKey(unit->id_) & 0xFF) != 0;
    stat10 = stat10Base;
    stat20 = stat20Base;
    stat18 = stat18Base;
    for (int i = 0; i < 6; ++i) {
        std::memcpy(&valuesB0[i].current, &valuesB0[i].base, sizeof(float));
        std::memcpy(&valuesE0[i].current, &valuesE0[i].base, sizeof(float));
    }

    double bonus = kZero;
    if (const char* record = SkillLevelRecord(kSkillBodyB0)) {
        double level = RecordValue(record);
        EI_NAN_AUDIT(level);
        if (level != kZero) bonus = level * valuesB0[1].current * kPercentB;
    }
    valuesB0[1].current = static_cast<float>(bonus + valuesB0[1].current);
    const char* record = SkillLevelRecord(kSkillStat10);
    stat10 = static_cast<float>((record ? RecordValue(record) : kZero) + stat10);
    record = SkillLevelRecord(kSkillStat18);
    stat18 = static_cast<float>((record ? RecordValue(record) : kZero) + stat18);
    record = SkillLevelRecord(kSkillStat20);
    stat20 = static_cast<float>((record ? RecordValue(record) : kZero) + stat20);
    for (UnitStatPair& protection : vitals->protection) std::memcpy(&protection.current, &protection.base, sizeof(float));

    // The strongest effect of each type.
    float strongest[kEffectTypes] = {};
    for (int i = 0; i < unit->effectCount; ++i) {
        MapEffect effect = EffectAt(unit, i);   // the original copies the whole record
        std::uint32_t type = effect.type;
        if (type >= static_cast<std::uint32_t>(kEffectTypes)) continue;
        float value = EffectValueField(effect);
        float old = strongest[type];
        strongest[type] = SelectLess(old, value, value, old);
    }
    // Fire, cold and lightning protection: their own effect or the general one (0x1A).
    float general = strongest[0x1A];
    vitals->protection[3].current = SelectLess(strongest[9], general, general, strongest[9]) + vitals->protection[3].current;
    vitals->protection[5].current = SelectLess(strongest[10], general, general, strongest[10]) + vitals->protection[5].current;
    vitals->protection[4].current = SelectLess(strongest[11], general, general, strongest[11]) + vitals->protection[4].current;
    valuesB0[0].current = strongest[12] + valuesB0[0].current;
    valuesB0[1].current = strongest[13] + valuesB0[1].current;
    valuesB0[2].current = strongest[14] + valuesB0[2].current;
    float lowered = valuesE0[0].current - strongest[15] * kPercentB;
    float l = lowered;
    valuesE0[0].current = SelectLess(kZero, l, l, kZero);
    lowered = valuesE0[3].current - strongest[16];
    l = lowered;
    valuesE0[3].current = SelectLess(kZero, l, l, kZero);
    lowered = valuesE0[2].current - strongest[17];
    l = lowered;
    valuesE0[2].current = SelectLess(kZero, l, l, kZero);
    valuesE0[4].current = strongest[19] + valuesE0[4].current;
    float hasteDelta = strongest[0x21] - strongest[0x22];

    if (keyed) {
        double curve =
            ExperienceCurve(&GameLogicSettings().hpVal[3], &experience, &GameLogicSettings().hpVal[2], &GameLogicSettings().hpVal[4]);
        double scaled = curve * (stat10 / GameLogicSettings().hpVal[0] * GameLogicSettings().hpVal[1]);
        vitals->maxHitPoints = static_cast<float>((LevelValueOf(this, kSkillHitPoints) * kPercent + kOne) * scaled);
        curve = ExperienceCurve(&GameLogicSettings().mpVal[3], &experience, &GameLogicSettings().mpVal[2], &GameLogicSettings().mpVal[4]);
        scaled = curve * (stat18 / GameLogicSettings().mpVal[0] * GameLogicSettings().mpVal[1]);
        vitals->maxMana = static_cast<float>((LevelValueOf(this, kSkillMana) * kPercent + kOne) * scaled);
    } else {
        std::memcpy(&vitals->maxHitPoints, &fixedMaxHitPoints, sizeof(float));
    }

    float limit = ScaledAtLeastOne(unit, kHitPointEffect, vitals->maxHitPoints);
    vitals->maxHitPoints = limit;
    float ratio = limit / slots[1].share;
    for (UnitStatSlot& slot : slots) {
        if (slot.kind <= 0) continue;
        float slotLimit = ratio * slot.share;
        slot.maximum = slotLimit;
        float m = slotLimit;
        float current = slot.current;
        slot.current = SelectLess(m, current, m, current);
    }

    if (keyed) {
        double strength = stat10 * kTwelve;
        std::memcpy(&sightCone, &kKeyedUnknown044, sizeof(float));
        capacity = static_cast<float>((LevelValueOf(this, kSkillCapacity) * kPercent + kOne) * strength);
    } else {
        std::memcpy(&capacity, &kNoCapacity, sizeof(float));
    }
    load = static_cast<float>(unit->TotalItemWeight());
    int unusedArg = keyed ? 1 : 0;   // the original passes the text-key byte (upper bytes garbage)
    loadFactor = movementKind == 1 ? LegsFactor(unusedArg) : kOne;
    speed = speedBase * loadFactor;
    float arms = ArmsFactor(unusedArg);
    armsFactor = arms;
    rateBase = keyed ? stat18 * kPointTwo + kTen : kUnkeyedRateBase;
    double value = (LevelValueOf(this, kSkillRate) * kPercentB + kOne) * arms * rateBase;
    rate = static_cast<float>(value);
    float hasted = static_cast<float>(value + hasteDelta);
    float h = hasted;
    rate = SelectLess(kOne, h, h, kOne);
    vitals->hitPoints = static_cast<float>(Reduce(vitals->maxHitPoints));
}
EI_REPLACE_METHOD(0x00524560, UnitStats::Recompute);

// --- Experience ------------------------------------------------------------------------------

namespace {
// pending += gain; a positive total moves into the experience (added unrounded).
void AddPending(UnitStats* stats, double gain) {
    double total = gain + stats->pendingExperience;
    stats->pendingExperience = static_cast<float>(total);
    if (total > kZero) {
        stats->pendingExperience = kZero;
        stats->experience = static_cast<float>(total + stats->experience);
    }
}
} // namespace

void UnitStats::AddExperience(float amount) { AddPending(this, amount); }

void UnitStats::AddScaledExperience(float amount) { AddPending(this, IntelligenceFactor(&stat20) * amount); }
EI_REPLACE_METHOD(0x00523960, UnitStats::AddScaledExperience);

void UnitStats::TakePendingExperience(float amount) { pendingExperience = pendingExperience - amount; }

namespace {
// The hit point and mana maxima after the experience or a base value changed: the curves with
// the skill levels' percentages (each product rounded once), the strengthen / weaken scale, the
// slots scaled along and the hit points. As GainExperience, RaiseIntelligence and RaiseSkill do.
void RefreshMaxima(UnitStats* stats, UnitVitals* vitals, CUnit* unit) {
    double curve = ExperienceCurve(&GameLogicSettings().hpVal[3], &stats->experience, &GameLogicSettings().hpVal[2],
                                   &GameLogicSettings().hpVal[4]);
    double scaled = curve * (stats->stat10 / GameLogicSettings().hpVal[0] * GameLogicSettings().hpVal[1]);
    float maxHitPoints = static_cast<float>((LevelValueOf(stats, kSkillHitPoints) * kPercent + kOne) * scaled);
    float limit = ScaledAtLeastOne(unit, kHitPointEffect, maxHitPoints);
    vitals->maxHitPoints = limit;
    curve = ExperienceCurve(&GameLogicSettings().mpVal[3], &stats->experience, &GameLogicSettings().mpVal[2],
                            &GameLogicSettings().mpVal[4]);
    scaled = curve * (stats->stat18 / GameLogicSettings().mpVal[0] * GameLogicSettings().mpVal[1]);
    vitals->maxMana = static_cast<float>((LevelValueOf(stats, kSkillMana) * kPercent + kOne) * scaled);
    stats->ScaleSlotMaxima(limit);
    vitals->hitPoints = static_cast<float>(stats->Reduce(vitals->maxHitPoints));
}

// 1.61 (the double at 0073E140) ^ (level / 007C5480) * 007C547C through _CIpow, unrounded: the
// experience a skill or attribute level is worth.
double LevelWorth(int level) {
    return libcrt::CiPow(1.61 /*0073E140*/, static_cast<double>(level) / ei::GameData<const float>(0x007C5480)) *
           ei::GameData<const float>(0x007C547C);
}

// Raises `levels[index]` by `amount` (a byte) and books what the new level is worth over the old
// one as experience (into the spent experience too). Then the maxima and a recomputation.
bool RaiseLevel(UnitStats* stats, std::uint8_t* levels, int index, int amount, CUnit* unit) {
    CEngineObject* server = ServerObjectOf(unit);
    if (server->GetClassId() != kClassServerObject) return false;
    UnitVitals* vitals = VitalsOf(server);
    std::uint8_t old = levels[index];
    int raised = (amount & 0xFF) + old;
    float worthNew = static_cast<float>(LevelWorth(raised));
    double worthOld = LevelWorth(old);
    levels[index] = static_cast<std::uint8_t>(old + amount);
    int cost = static_cast<int>(gamecrt::Ftol(worthNew - worthOld));
    double total = static_cast<double>(cost) + stats->pendingExperience;
    stats->pendingExperience = static_cast<float>(total);
    if (total > kZero) {
        stats->experience = static_cast<float>(total + stats->experience);
        stats->pendingExperience = kZero;
        stats->spentExperience = static_cast<float>(total + stats->spentExperience);
    }
    RefreshMaxima(stats, vitals, unit);
    stats->Recompute(vitals, unit);
    return true;
}

} // namespace

void UnitStats::GainExperience(CUnit* unit, float amount) {
    CEngineObject* server = ServerObjectOf(unit);
    if (server->GetClassId() != kClassServerObject) return;
    AddPending(this, IntelligenceFactor(&stat20) * amount);
    RefreshMaxima(this, VitalsOf(server), unit);
}
EI_REPLACE_METHOD(0x005239B0, UnitStats::GainExperience);

bool UnitStats::RaiseSkill(int skill, int amount, CUnit* unit) { return RaiseLevel(this, skills, skill, amount, unit); }
EI_REPLACE_METHOD(0x00528AB0, UnitStats::RaiseSkill);

bool UnitStats::RaiseAttribute(int index, int amount, CUnit* unit) {
    return RaiseLevel(this, attributes, index, amount, unit);
}
EI_REPLACE_METHOD(0x00528DA0, UnitStats::RaiseAttribute);

bool UnitStats::RaiseStrength(int amount, CUnit* unit) {
    CEngineObject* server = ServerObjectOf(unit);
    if (server->GetClassId() != kClassServerObject) return false;
    UnitVitals* vitals = VitalsOf(server);
    stat10Base = static_cast<float>(static_cast<double>(amount) + stat10Base);
    double curve = ExperienceCurve(&GameLogicSettings().hpVal[3], &experience, &GameLogicSettings().hpVal[2], &GameLogicSettings().hpVal[4]);
    float scaled = static_cast<float>(curve * (stat10 / GameLogicSettings().hpVal[0] * GameLogicSettings().hpVal[1]));
    const char* record = SkillLevelRecord(kSkillHitPoints);
    float maxHitPoints =
        static_cast<float>(((record ? RecordValue(record) : kZero) * kPercent + kOne) * scaled);
    vitals->maxHitPoints = ScaledAtLeastOne(unit, kHitPointEffect, maxHitPoints);
    vitals->maxMana = static_cast<float>(ManaCurve());
    ScaleSlotMaxima(vitals->maxHitPoints);
    vitals->hitPoints = static_cast<float>(Reduce(vitals->maxHitPoints));
    unit->baseScale.x = unit->baseScale.x + kPointTwo;
    unit->scale.x = unit->scale.x + kPointTwo;
    unit->MarkChanged(1);
    Recompute(vitals, unit);
    unit->MarkChanged(4);
    return true;
}
EI_REPLACE_METHOD(0x00529090, UnitStats::RaiseStrength);

bool UnitStats::RaiseDexterity(int amount, CUnit* unit) {
    CEngineObject* server = ServerObjectOf(unit);
    if (server->GetClassId() != kClassServerObject) return false;
    UnitVitals* vitals = VitalsOf(server);
    stat18Base = static_cast<float>(static_cast<double>(amount) + stat18Base);
    double curve = ExperienceCurve(&GameLogicSettings().hpVal[3], &experience, &GameLogicSettings().hpVal[2], &GameLogicSettings().hpVal[4]);
    float scaled = static_cast<float>(curve * (stat10 / GameLogicSettings().hpVal[0] * GameLogicSettings().hpVal[1]));
    float maxHitPoints =
        static_cast<float>((SkillLevelValueCopy(kSkillHitPoints) * kPercent + kOne) * scaled);
    float boosted = static_cast<float>(HitPointEffectScale(unit) * maxHitPoints);
    float one = kOne;
    float b = boosted;
    vitals->maxHitPoints = SelectLess(one, b, b, one);
    curve = ExperienceCurve(&GameLogicSettings().mpVal[3], &experience, &GameLogicSettings().mpVal[2], &GameLogicSettings().mpVal[4]);
    scaled = static_cast<float>(curve * (stat18 / GameLogicSettings().mpVal[0] * GameLogicSettings().mpVal[1]));
    vitals->maxMana =
        static_cast<float>((SkillLevelValueCopy(kSkillMana) * kPercent + kOne) * scaled);
    ScaleSlotMaxima(vitals->maxHitPoints);
    vitals->hitPoints = static_cast<float>(Reduce(vitals->maxHitPoints));
    float lowered = unit->baseScale.y - kPointTwo;
    float l = lowered;
    unit->baseScale.y = SelectLess(kZero, l, l, kZero);
    lowered = unit->scale.y - kPointTwo;
    l = lowered;
    unit->scale.y = SelectLess(kZero, l, l, kZero);
    unit->MarkChanged(1);
    Recompute(vitals, unit);
    unit->MarkChanged(4);
    return true;
}
EI_REPLACE_METHOD(0x00529220, UnitStats::RaiseDexterity);

bool UnitStats::RaiseIntelligence(int amount, CUnit* unit) {
    CEngineObject* server = ServerObjectOf(unit);
    if (server->GetClassId() != kClassServerObject) return false;
    UnitVitals* vitals = VitalsOf(server);
    stat20Base = static_cast<float>(static_cast<double>(amount) + stat20Base);
    RefreshMaxima(this, vitals, unit);
    Recompute(vitals, unit);
    return true;
}
EI_REPLACE_METHOD(0x00529400, UnitStats::RaiseIntelligence);

double UnitStats::SkillLevelValueCopy(int skill) { return LevelValueOf(this, skill); }
