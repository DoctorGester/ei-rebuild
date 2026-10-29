// Creature statistics that change while the game runs: the recomputation after hits, heals and
// item changes (UnitStats::Recompute), regeneration every world update (UnitVitals::Regenerate),
// the body slots' healing, the experience curves and experience gain. See unit.h and
// docs/notes/units.md ("Statistics in play").
#include "map_object.h"
#include "spell.h"
#include "unit.h"

#include "object_tables.h"
#include "unit_db.h"

#include "logic_settings.h"
#include "lib/crt.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/float_select.h"

#include <bit>
#include <cstring>

// The formulas must keep the x87 order of operations (see unit_stats.cpp). Values kept on the FPU stack are
// double locals; values stored to a float are float locals.

namespace {
constexpr float kPercent = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(kPercent) == 0x3C23D70Au);
constexpr float kPercentB = 0.01f;      // 0.01, a second copy
static_assert(std::bit_cast<std::uint32_t>(kPercentB) == 0x3C23D70Au);
constexpr float kHitPointEffect = 0.005f;
static_assert(std::bit_cast<std::uint32_t>(kHitPointEffect) == 0x3BA3D70Au);
constexpr float kDamageEffect = 0.003f;
static_assert(std::bit_cast<std::uint32_t>(kDamageEffect) == 0x3B449BA6u);
constexpr float kTwelve = 12.0f;
constexpr float kPointTwo = 0.2f;
static_assert(std::bit_cast<std::uint32_t>(kPointTwo) == 0x3E4CCCCDu);
constexpr float kTen = 10.0f;
constexpr float kRevived = 0.001f;
static_assert(std::bit_cast<std::uint32_t>(kRevived) == 0x3A83126Fu);
constexpr double kMinHeal = 0.001;                                  // a double
constexpr float kNoCapacity = 1000000.0f;
constexpr float kKeyedUnknown044 = -2.0f;
constexpr float kUnkeyedRateBase = 15.0f;                           // 0x41700000

constexpr int kRegenPeriod = 15;
constexpr int kSlotArms = 2;
constexpr int kSlotLegs = 3;

constexpr int kEffectTypes = 0x28;

constexpr int kSkillBodyB0 = 15;      // raises perception[1] by its percentage
constexpr int kSkillHitPoints = 16;
constexpr int kSkillMana = 17;
constexpr int kSkillHitPointRegen = 18;
constexpr int kSkillManaRegen = 19;
constexpr int kSkillRate = 20;
constexpr int kSkillCapacity = 21;
constexpr int kSkillStat10 = 23;
constexpr int kSkillStat18 = 24;
constexpr int kSkillStat20 = 25;

// base ^ (ln(experience / divisor) / ln(logBase)) with the CRT's _CIpow. Unrounded.
double ExperienceCurve(const float* base, const float* experience, const float* divisor, const float* logBase) {
    return libcrt::CiPow(*base, x87::flog(static_cast<double>(*experience) / *divisor) / x87::flog(*logBase));
}

// 1.04 (a double) ^ (intelligence - 25) through _CIpow, unrounded.
double IntelligenceFactor(const float* intelligence) {
    return libcrt::CiPow(1.04,
                         static_cast<double>(*intelligence) - 25.0f);
}

// The level value of skill `skill` (0 without a level or a record), unsigned and exact.
double LevelValueOf(const UnitStats* stats, int skill) {
    std::uint8_t level = stats->skillLevels[skill];
    if (level == 0) return 0.0f;
    const SkillLevelRecord* record = unitdb::SkillLevelAt(static_cast<std::uint32_t>(skill * 3 - 1 + level));
    if (!record) return 0.0f;
    return record->Value();
}


MapEffect EffectAt(const CUnit* unit, int index) { return unit->effects[index]; }
float EffectValueField(const MapEffect& effect) {
    float value;
    value = std::bit_cast<float>(effect.value);
    return value;
}

UnitVitals* ServerVitals(CUnit* unit) {
    CMapObjectServer* server = unit->ServerCompanion();
    return server->GetClassId() == ClassId::MapObjectServer ? &server->vitals : nullptr;
}

// strengthen / weaken scale: (1 + v(0x1D) * k) / (1 + v(0x1E) * k), the numerator stored as a
// float; 1 without a unit. Unrounded.
double EffectScale(CUnit* unit, float k);

double EffectScale(CUnit* unit, float k) {
    if (!unit) return 1.0f;
    float strengthen = static_cast<float>(unit->EffectValue(SpellType::Strengthen) * k + 1.0f);
    return strengthen / (unit->EffectValue(SpellType::Weaken) * k + 1.0f);
}

float ScaledAtLeastOne(CUnit* unit, float k, float value) {
    double scale = EffectScale(unit, k);
    float scaled = static_cast<float>(scale * value);
    float one = 1.0f;
    float s = scaled;
    return SelectLess(one, s, s, one);
}

float LowestSlotRatio(const UnitStats* stats, int type) {
    float lowest = 1.0f;
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
    if (lowest <= g_logicSettings.damageLevel2) return g_logicSettings.damageLevel2Value;
    if (lowest <= g_logicSettings.damageLevel1) return g_logicSettings.damageLevel1Value;
    return 1.0f;
}

void AddPending(UnitStats* stats, double gain) {
    double total = gain + stats->pendingExperience;
    stats->pendingExperience = static_cast<float>(total);
    if (total > 0.0f) {
        stats->pendingExperience = 0.0f;
        stats->experience = static_cast<float>(total + stats->experience);
    }
}

void RefreshMaxima(UnitStats* stats, UnitVitals* vitals, CUnit* unit) {
    double curve = ExperienceCurve(&g_logicSettings.hpVal[3], &stats->experience, &g_logicSettings.hpVal[2],
                                   &g_logicSettings.hpVal[4]);
    double scaled = curve * (stats->strength / g_logicSettings.hpVal[0] * g_logicSettings.hpVal[1]);
    float maxHitPoints = static_cast<float>((LevelValueOf(stats, kSkillHitPoints) * kPercent + 1.0f) * scaled);
    float limit = ScaledAtLeastOne(unit, kHitPointEffect, maxHitPoints);
    vitals->maxHitPoints = limit;
    curve = ExperienceCurve(&g_logicSettings.mpVal[3], &stats->experience, &g_logicSettings.mpVal[2],
                            &g_logicSettings.mpVal[4]);
    scaled = curve * (stats->dexterity / g_logicSettings.mpVal[0] * g_logicSettings.mpVal[1]);
    vitals->maxMana = static_cast<float>((LevelValueOf(stats, kSkillMana) * kPercent + 1.0f) * scaled);
    stats->ScaleSlotMaxima(limit);
    vitals->hitPoints = static_cast<float>(stats->Reduce(vitals->maxHitPoints));
}

// 1.61 (a double) ^ (level / divisor) * factor through _CIpow, unrounded: the experience a skill or
// attribute level is worth.
double LevelWorth(int level) {
    return libcrt::CiPow(1.61, static_cast<double>(level) / g_logicSettings.skillVal[1]) *
           g_logicSettings.skillVal[0];
}

bool RaiseLevel(UnitStats* stats, std::uint8_t* levels, int index, int amount, CUnit* unit) {
    UnitVitals* vitals = ServerVitals(unit);
    if (!vitals) return false;
    std::uint8_t old = levels[index];
    int raised = (amount & 0xFF) + old;
    float worthNew = static_cast<float>(LevelWorth(raised));
    double worthOld = LevelWorth(old);
    levels[index] = static_cast<std::uint8_t>(old + amount);
    int cost = static_cast<int>(libcrt::Ftol(worthNew - worthOld));
    double total = static_cast<double>(cost) + stats->pendingExperience;
    stats->pendingExperience = static_cast<float>(total);
    if (total > 0.0f) {
        stats->experience = static_cast<float>(total + stats->experience);
        stats->pendingExperience = 0.0f;
        stats->spentExperience = static_cast<float>(total + stats->spentExperience);
    }
    RefreshMaxima(stats, vitals, unit);
    stats->Recompute(vitals, unit);
    return true;
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
            (LevelValueOf(stats, kSkillManaRegen) * kPercent + 1.0f) * regen * maxMana + mana);
        float limit = maxMana;
        float r = raised;
        mana = SelectLess(limit, r, limit, r);
        changed = true;
    }
    EI_NAN_AUDIT(hitPoints, maxHitPoints);
    if (hitPoints < maxHitPoints) {
        double regen = hitPointRegen;
        float rate = static_cast<float>((LevelValueOf(stats, kSkillHitPointRegen) * kPercent + 1.0f) * regen);
        if (unit) {
            double boost = unit->EffectValue(SpellType::Regeneration);
            if (boost > 0.0f) rate = static_cast<float>(boost * rate);
        }
        float scale = maxHitPoints;
        stats->HealSlots(scale, static_cast<float>(scale * rate));
        hitPoints = static_cast<float>(stats->Reduce(maxHitPoints));
        changed = true;
    }
    return changed;
}

// --- Body slots ------------------------------------------------------------------------------

void UnitStats::HealSlots(float scale, float amount) {
    float magnitude = AbsBits(amount);
    // Kept as the game's test: a NaN amount heals nothing. It occurs in every run: LearnSkill's heal (the
    // default NaN) while a new unit's stats are being set up (the NaN audit).
    if (!(magnitude > kMinHeal)) return;
    double total = 0.0;   // accumulated on the FPU stack
    for (const UnitStatSlot& slot : slots) {
        if (slot.kind > 1) EI_NAN_AUDIT(slot.current, slot.maximum);
        if (slot.kind > 1 && slot.current < slot.maximum) total += slot.factor;
    }
    EI_NAN_AUDIT(total);
    if (total == 0.0f) return;
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
}

void UnitStats::Heal(UnitVitals* vitals, float amount) {
    HealSlots(vitals->maxHitPoints, amount);
    vitals->hitPoints = static_cast<float>(Reduce(vitals->maxHitPoints));
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
        if (slot.kind >= 2) slot.current = slot.maximum;   // dword copy
    }
    UnitStatSlot* end = slots + 6;
    return &end->maximum;   // where the loop pointer stops
}

void* UnitStats::RestoreSlots() {
    for (UnitStatSlot& slot : slots) {
        if (slot.kind > 0) {
            slot.kind = 3;
            slot.current = slot.maximum;
        }
    }
    UnitStatSlot* end = slots + 6;
    return &end->kind;   // where the loop pointer stops
}

float UnitStats::LegsFactor() { return RatioFactor(LowestSlotRatio(this, kSlotLegs)); }

float UnitStats::ArmsFactor() { return RatioFactor(LowestSlotRatio(this, kSlotArms)); }

int UnitStats::MovementState(char checkLoad) {
    float lowest = LowestSlotRatio(this, kSlotLegs);
    if (checkLoad != 0 && load > capacity) return 2;
    EI_NAN_AUDIT(lowest);
    if (lowest < g_logicSettings.damageLevelRunLimit) return 1;
    return 0;
}

// --- Maxima ----------------------------------------------------------------------------------

double UnitStats::HitPointCurve() {
    double curve = ExperienceCurve(&g_logicSettings.hpVal[3], &experience, &g_logicSettings.hpVal[2], &g_logicSettings.hpVal[4]);
    double scaled = curve * (strength / g_logicSettings.hpVal[0] * g_logicSettings.hpVal[1]);
    return (LevelValueOf(this, kSkillHitPoints) * kPercent + 1.0f) * scaled;
}

double UnitStats::ManaCurve() {
    double curve =
        ExperienceCurve(&g_logicSettings.mpVal[3], &experience, &g_logicSettings.mpVal[2], &g_logicSettings.mpVal[4]);
    double scaled = curve * (dexterity / g_logicSettings.mpVal[0] * g_logicSettings.mpVal[1]);
    return (LevelValueOf(this, kSkillMana) * kPercent + 1.0f) * scaled;
}

double UnitStats::RateValue() {
    float base = dexterity * kPointTwo + kTen;
    float level = static_cast<float>(LevelValueOf(this, kSkillRate));
    float arms = ArmsFactor();
    return (level * kPercentB + 1.0f) * arms * base;
}

float ScaleDamageByEffects(CUnit* unit, float value) { return ScaledAtLeastOne(unit, kDamageEffect, value); }

double HitPointEffectScale(CUnit* unit) { return EffectScale(unit, kHitPointEffect); }

float ScaleHitPointsByEffects(CUnit* unit, float value) {
    return ScaledAtLeastOne(unit, kHitPointEffect, value);
}

void UnitStats::RecomputeHitPoints(UnitVitals* vitals, CUnit* unit) {
    if (IsTextKey(unit->id)) {
        vitals->maxHitPoints = static_cast<float>(HitPointCurve());
    } else {
        vitals->maxHitPoints = fixedMaxHitPoints;
    }
    float limit = ScaledAtLeastOne(unit, kHitPointEffect, vitals->maxHitPoints);
    vitals->maxHitPoints = limit;
    ScaleSlotMaxima(limit);
    double hitPoints = Reduce(vitals->maxHitPoints);
    vitals->hitPoints = static_cast<float>(hitPoints);
    if (hitPoints > 0.0f) flags |= UnitStatsFlag::Alive;
}

void UnitStats::Recompute(UnitVitals* vitals, CUnit* unit) {
    bool keyed = IsTextKey(unit->id);
    strength = strengthBase;
    intelligence = intelligenceBase;
    dexterity = dexterityBase;
    for (int i = 0; i < 6; ++i) {
        perception[i].current = perception[i].base;
        conspicuity[i].current = conspicuity[i].base;
    }

    double bonus = 0.0;
    if (const ::SkillLevelRecord* record = SkillLevelRecord(kSkillBodyB0)) {
        double level = record->Value();
        EI_NAN_AUDIT(level);
        if (level != 0.0f) bonus = level * perception[1].current * kPercentB;
    }
    perception[1].current = static_cast<float>(bonus + perception[1].current);
    const ::SkillLevelRecord* record = SkillLevelRecord(kSkillStat10);
    strength = static_cast<float>((record ? record->Value() : 0.0f) + strength);
    record = SkillLevelRecord(kSkillStat18);
    dexterity = static_cast<float>((record ? record->Value() : 0.0f) + dexterity);
    record = SkillLevelRecord(kSkillStat20);
    intelligence = static_cast<float>((record ? record->Value() : 0.0f) + intelligence);
    for (UnitStatPair& protection : vitals->protection) protection.current = protection.base;

    float strongest[kEffectTypes] = {};
    for (int i = 0; i < std::ssize(unit->effects); ++i) {
        MapEffect effect = EffectAt(unit, i);   // copies the whole record
        std::uint32_t type = effect.type;
        if (type >= static_cast<std::uint32_t>(kEffectTypes)) continue;
        float value = EffectValueField(effect);
        float old = strongest[type];
        strongest[type] = SelectLess(old, value, value, old);
    }
    float general = strongest[0x1A];
    vitals->protection[3].current = SelectLess(strongest[9], general, general, strongest[9]) + vitals->protection[3].current;
    vitals->protection[5].current = SelectLess(strongest[10], general, general, strongest[10]) + vitals->protection[5].current;
    vitals->protection[4].current = SelectLess(strongest[11], general, general, strongest[11]) + vitals->protection[4].current;
    perception[0].current = strongest[12] + perception[0].current;
    perception[1].current = strongest[13] + perception[1].current;
    perception[2].current = strongest[14] + perception[2].current;
    float lowered = conspicuity[0].current - strongest[15] * kPercentB;
    float l = lowered;
    conspicuity[0].current = SelectLess(0.0f, l, l, 0.0f);
    lowered = conspicuity[3].current - strongest[16];
    l = lowered;
    conspicuity[3].current = SelectLess(0.0f, l, l, 0.0f);
    lowered = conspicuity[2].current - strongest[17];
    l = lowered;
    conspicuity[2].current = SelectLess(0.0f, l, l, 0.0f);
    conspicuity[4].current = strongest[19] + conspicuity[4].current;
    float hasteDelta = strongest[0x21] - strongest[0x22];

    if (keyed) {
        double curve =
            ExperienceCurve(&g_logicSettings.hpVal[3], &experience, &g_logicSettings.hpVal[2], &g_logicSettings.hpVal[4]);
        double scaled = curve * (strength / g_logicSettings.hpVal[0] * g_logicSettings.hpVal[1]);
        vitals->maxHitPoints = static_cast<float>((LevelValueOf(this, kSkillHitPoints) * kPercent + 1.0f) * scaled);
        curve = ExperienceCurve(&g_logicSettings.mpVal[3], &experience, &g_logicSettings.mpVal[2], &g_logicSettings.mpVal[4]);
        scaled = curve * (dexterity / g_logicSettings.mpVal[0] * g_logicSettings.mpVal[1]);
        vitals->maxMana = static_cast<float>((LevelValueOf(this, kSkillMana) * kPercent + 1.0f) * scaled);
    } else {
        vitals->maxHitPoints = fixedMaxHitPoints;
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
        double scaledStrength = strength * kTwelve;
        sightCone = kKeyedUnknown044;
        capacity = static_cast<float>((LevelValueOf(this, kSkillCapacity) * kPercent + 1.0f) * scaledStrength);
    } else {
        capacity = kNoCapacity;
    }
    load = static_cast<float>(unit->TotalItemWeight());
    loadFactor = movementKind == 1 ? LegsFactor() : 1.0f;
    speed = speedBase * loadFactor;
    float arms = ArmsFactor();
    armsFactor = arms;
    rateBase = keyed ? dexterity * kPointTwo + kTen : kUnkeyedRateBase;
    double value = (LevelValueOf(this, kSkillRate) * kPercentB + 1.0f) * arms * rateBase;
    rate = static_cast<float>(value);
    float hasted = static_cast<float>(value + hasteDelta);
    float h = hasted;
    rate = SelectLess(1.0f, h, h, 1.0f);
    vitals->hitPoints = static_cast<float>(Reduce(vitals->maxHitPoints));
}

// --- Experience ------------------------------------------------------------------------------

void UnitStats::AddExperience(float amount) { AddPending(this, amount); }

void UnitStats::AddScaledExperience(float amount) { AddPending(this, IntelligenceFactor(&intelligence) * amount); }

void UnitStats::TakePendingExperience(float amount) { pendingExperience = pendingExperience - amount; }

void UnitStats::GainExperience(CUnit* unit, float amount) {
    UnitVitals* vitals = ServerVitals(unit);
    if (!vitals) return;
    AddPending(this, IntelligenceFactor(&intelligence) * amount);
    RefreshMaxima(this, vitals, unit);
}

bool UnitStats::RaiseSkill(int skill, int amount, CUnit* unit) { return RaiseLevel(this, skills, skill, amount, unit); }

bool UnitStats::RaiseAttribute(int index, int amount, CUnit* unit) {
    return RaiseLevel(this, attributes, index, amount, unit);
}

bool UnitStats::RaiseStrength(int amount, CUnit* unit) {
    UnitVitals* vitals = ServerVitals(unit);
    if (!vitals) return false;
    strengthBase = static_cast<float>(static_cast<double>(amount) + strengthBase);
    double curve = ExperienceCurve(&g_logicSettings.hpVal[3], &experience, &g_logicSettings.hpVal[2], &g_logicSettings.hpVal[4]);
    float scaled = static_cast<float>(curve * (strength / g_logicSettings.hpVal[0] * g_logicSettings.hpVal[1]));
    const ::SkillLevelRecord* record = SkillLevelRecord(kSkillHitPoints);
    float maxHitPoints =
        static_cast<float>(((record ? record->Value() : 0.0f) * kPercent + 1.0f) * scaled);
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

bool UnitStats::RaiseDexterity(int amount, CUnit* unit) {
    UnitVitals* vitals = ServerVitals(unit);
    if (!vitals) return false;
    dexterityBase = static_cast<float>(static_cast<double>(amount) + dexterityBase);
    double curve = ExperienceCurve(&g_logicSettings.hpVal[3], &experience, &g_logicSettings.hpVal[2], &g_logicSettings.hpVal[4]);
    float scaled = static_cast<float>(curve * (strength / g_logicSettings.hpVal[0] * g_logicSettings.hpVal[1]));
    float maxHitPoints =
        static_cast<float>((SkillLevelValueCopy(kSkillHitPoints) * kPercent + 1.0f) * scaled);
    float boosted = static_cast<float>(HitPointEffectScale(unit) * maxHitPoints);
    float one = 1.0f;
    float b = boosted;
    vitals->maxHitPoints = SelectLess(one, b, b, one);
    curve = ExperienceCurve(&g_logicSettings.mpVal[3], &experience, &g_logicSettings.mpVal[2], &g_logicSettings.mpVal[4]);
    scaled = static_cast<float>(curve * (dexterity / g_logicSettings.mpVal[0] * g_logicSettings.mpVal[1]));
    vitals->maxMana =
        static_cast<float>((SkillLevelValueCopy(kSkillMana) * kPercent + 1.0f) * scaled);
    ScaleSlotMaxima(vitals->maxHitPoints);
    vitals->hitPoints = static_cast<float>(Reduce(vitals->maxHitPoints));
    float lowered = unit->baseScale.y - kPointTwo;
    float l = lowered;
    unit->baseScale.y = SelectLess(0.0f, l, l, 0.0f);
    lowered = unit->scale.y - kPointTwo;
    l = lowered;
    unit->scale.y = SelectLess(0.0f, l, l, 0.0f);
    unit->MarkChanged(1);
    Recompute(vitals, unit);
    unit->MarkChanged(4);
    return true;
}

bool UnitStats::RaiseIntelligence(int amount, CUnit* unit) {
    UnitVitals* vitals = ServerVitals(unit);
    if (!vitals) return false;
    intelligenceBase = static_cast<float>(static_cast<double>(amount) + intelligenceBase);
    RefreshMaxima(this, vitals, unit);
    Recompute(vitals, unit);
    return true;
}

double UnitStats::SkillLevelValueCopy(int skill) { return LevelValueOf(this, skill); }
