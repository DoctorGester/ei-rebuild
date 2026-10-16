// A creature's starting vitals and statistics from its prototype records, the prototype applied
// to a new map object, and the map object factory. See unit_create.h and docs/notes/units.md ("Creation,
// databases, save").
#include "unit_create.h"

#include "item.h"
#include "map_object.h"
#include "object_tables.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_units.h"

#include <bit>
#include <cstring>

// Values the original keeps on the FPU stack are double locals; values it stores are floats.

namespace {
// The records (unit_db.h): prototypes (007AF358), models (007AF398), NPCs (007AF378) and the
// models' level records (007AF3B8).
using Prototype = UnitPrototypeRecord;
using Model = UnitModelRecord;
using Npc = UnitNpcRecord;

constexpr int kSkillLevelRecordSize = 0x44;   // 007AF3B8

constexpr float kThird = std::bit_cast<float>(0x3EAAAAABu);     // 0073BAA8
constexpr float kHalf = 0.5f;                                    // 0073B86C
constexpr std::uint32_t kStat25 = 0x41C80000;                    // 25.0
constexpr std::uint32_t kRate15 = 0x41700000;                    // 15.0
constexpr std::uint32_t kDefaultStat = 0x42480000;               // 50.0
constexpr std::uint32_t kDefaultModeSpeed = 0x3E088889;          // 0.13333334
constexpr std::uint32_t kDefaultSpeed = 0x3E4CCCCD;              // 0.2
constexpr std::uint32_t kOne = 0x3F800000;
constexpr std::uint32_t kHundred = 0x42C80000;
constexpr std::uint32_t kThousand = 0x447A0000;
constexpr std::uint32_t kTen = 0x41200000;
constexpr int kSlotWorking = 3;

// pi and 1/180 (0073DCA8 / 0073BA30) and 90 (0073BAB8): volatile so the products are computed
// at run time with the FPU's precision, as the original does.
volatile double g_pi = 3.141592653589793;
volatile double g_perDegree = std::bit_cast<double>(0x3F76C16C16C16C17ull);
volatile double g_ninety = 90.0;

// The original stores these as dwords (bit patterns), not through the FPU.
void SetBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, sizeof(field)); }
template <typename To, typename From>
void CopyDword(To& to, const From& from) {
    static_assert(sizeof(To) == 4 && sizeof(From) == 4);
    std::memcpy(&to, &from, 4);
}

// FLD [src]; FST [a]; FSTP [b]: a float copied through the FPU to two places.
void FpuCopy2(float* a, float* b, const float* src) {
    x87::CopyFloat(a, src);
    x87::CopyFloat(b, src);
}

std::uint8_t RoundToByte(float value) { return static_cast<std::uint8_t>(x87::fistp(value)); }

// Not rewritten / rewritten elsewhere, through their game addresses:
// 00523370 UnitVitals::Reset, 005233E0 UnitStats::Reset, 00523960 AddScaledExperience,
// 00525AF0 RefillSlots, all in unit_vitals.cpp / unit_stats_pack.cpp.
// 0051FE00. Skill database: the skill named `name` by _mbsicmp, or null. Not rewritten.
void* FindSkill(void* database, void*, const void* name) { return SkillFindOriginal(database, nullptr, name); }
// 00526450. Statistics: learns `skill` of the skill database. Not rewritten.
void LearnSkillAt(UnitStats* stats, void*, void* database, void* record) {
    StatsLearnSkillOriginal(stats, nullptr, database, record);
}
// 00524CD0. Statistics: recomputes the current values at `scale`. Not rewritten.
// This file's functions through their addresses, so their verify wrappers stay in the path.
// 0050F050 CMapObject::SetServerData, 00517700 CObject3D::SetFigureScale, 005308A0
// CUnit::SetPrototype23C, rewritten elsewhere.
__declspec(naked) std::uintptr_t __fastcall SetServerDataAt(CEngineObject*, void*, const void*) EI_ORIGINAL(0x0050F050)
__declspec(naked) std::uint32_t __fastcall SetPrototype23CAt(CEngineObject*, void*, const char*) EI_ORIGINAL(0x005308A0)

// The records by name (null when there is none).
const Prototype* FindPrototype(const char* name) {
    return reinterpret_cast<const Prototype*>(UnitPrototypeFind(&unitdb::PrototypeTable(), nullptr, name));
}
const Npc* FindNpc(const char* name) { return reinterpret_cast<const Npc*>(unitdb::NpcTable().FindByName(name)); }

// The prototype's model; its index is read even from a null prototype (the original faults there).
const Model* ModelOf(const Prototype* prototype) {
    auto index = static_cast<std::uint32_t>(static_cast<const volatile Prototype*>(prototype)->modelIndex);
    return reinterpret_cast<const Model*>(unitdb::Models().RecordAt(index, unitdb::kModelSize));
}

// --- The statistics both builders fill the same way ---------------------------------------

// cos(angle / 2) with the angle in degrees, stored to a float.
float HalfAngleCosine(const Model* model) {
    double half = static_cast<double>(model->angle) * kHalf;
    double perDegree = g_pi * g_perDegree;
    return static_cast<float>(x87::fcos(half * perDegree));
}

float RightAngleCosine() {
    double perDegree = g_pi * g_perDegree;
    return static_cast<float>(x87::fcos(perDegree * g_ninety));
}

// Attribute and skill bytes: from the NPC record, or rounded from the prototype.
void SetAttributesAndSkills(UnitStats* stats, const Prototype* prototype, const Npc* npc) {
    if (npc) {
        static_assert(offsetof(Npc, skills) - offsetof(Npc, attributes) == 3);
        std::memcpy(stats->attributes, npc->attributes, 3 + 8);   // the attributes and the skills
        return;
    }
    for (int i = 0; i < 3; ++i) stats->attributes[i] = RoundToByte(prototype->skillLevel);
    for (int i = 0; i < 8; ++i) stats->skills[i] = RoundToByte(prototype->skillLevel);
    stats->attributes[0] = RoundToByte(prototype->attributes01);
    stats->attributes[1] = RoundToByte(prototype->attributes01);
    stats->attributes[2] = RoundToByte(prototype->attribute2);
    for (int i = 0; i < 3; ++i) stats->skills[3 + i] = static_cast<std::uint8_t>(gamecrt::Ftol(prototype->skills3to5[i]));
}

// The body slots from the model: a slot with a share is working, with its type and vital mask
// from its level record (007AF3B8) and no layers; the others unused.
void SetSlots(UnitStats* stats, const Model* model) {
    const RecordTable& levels = *reinterpret_cast<const RecordTable*>(0x007AF3B8);
    for (int i = 0; i < 6; ++i) {
        const UnitModelSlot& entry = model->slots[i];
        UnitStatSlot& slot = stats->slots[i];
        float share = entry.share;
        if (!(share < 0.0f || share > 0.0f)) {   // zero (or NaN)
            slot.kind = 0;
            continue;
        }
        const auto* level = reinterpret_cast<const UnitLevelRecord*>(
            levels.RecordAt(static_cast<std::uint32_t>(entry.levelIndex), kSkillLevelRecordSize));
        slot.type = level->slotType;
        CopyDword(slot.share, entry.share);
        CopyDword(slot.factor, entry.factor);
        slot.vitalMask = level->vitalMask;
        slot.layers[0].layerMask = 0;
        slot.layers[0].state = 0;
        slot.layers[1].state = 0;
        slot.layers[2].state = 0;
        slot.kind = kSlotWorking;
    }
}

// The combat values and damage weights.
void SetCombat(UnitStats* stats, const Prototype* prototype, const Model* model) {
    for (int i = 0; i < 5; ++i) CopyDword(stats->combat[i], prototype->combat[i]);
    CopyDword(stats->damage, prototype->damage);
    CopyDword(stats->damageSpread, prototype->damageSpread);
    static_assert(sizeof(stats->damageWeights) == sizeof(model->damageWeights));
    std::memcpy(stats->damageWeights, model->damageWeights, sizeof(stats->damageWeights));
    CopyDword(stats->naturalWeapon, prototype->naturalWeapon);
}

// Experience and the skills an NPC knows; the spent experience then equals it.
void LearnNpcSkills(UnitStats* stats, const Npc* npc) {
    stats->spentExperience = 0.0f;
    if (!npc) return;
    UnitStatsAddExperience(stats, nullptr, npc->experience);
    for (const mfc::CString* name = npc->skillNames.begin(); name != npc->skillNames.end(); ++name) {
        void* skill = FindSkill(SkillDb(), nullptr, static_cast<const char*>(*name));
        if (skill) LearnSkillAt(stats, nullptr, SkillDb(), skill);
    }
    std::memcpy(&stats->spentExperience, &stats->experience, 4);
}

} // namespace

// --- Name lookups -----------------------------------------------------------------------------

EI_REPLACE_METHOD(0x005144D0, UnitPrototypeTable::FindByName);

char* NpcPrototypeTable::FindByName(const char* name) {
    stlp::string key(name);
    stlp::string pairKey(key);
    char* record = byName[pairKey];
    return record;
}
EI_REPLACE_METHOD(0x005145D0, NpcPrototypeTable::FindByName);

// --- Builders ---------------------------------------------------------------------------------

int UnitStatsBuilder::SetDefaults(UnitVitals* vitals, UnitStats* stats) {
    if (!stats) {
        SetBits(vitals->hitPoints, kThousand);
        SetBits(vitals->maxHitPoints, kThousand);
        SetBits(vitals->hitPointRegen, kTen);
        SetBits(vitals->mana, kThousand);
        SetBits(vitals->maxMana, kThousand);
        SetBits(vitals->manaRegen, kTen);
        for (UnitStatPair& protection : vitals->protection) {
            SetBits(protection.current, kHundred);
            SetBits(protection.base, kHundred);
        }
        return 1;
    }
    SetBits(stats->experience, 0);
    SetBits(stats->stat10Base, kDefaultStat);
    SetBits(stats->stat18Base, kDefaultStat);
    SetBits(stats->stat20Base, kDefaultStat);
    for (float& modeSpeed : stats->modeSpeeds) SetBits(modeSpeed, kDefaultModeSpeed);
    SetBits(stats->spentExperience, 0);
    SetBits(stats->speedBase, kDefaultSpeed);
    SetBits(stats->rateBase, kOne);
    SetBits(stats->capacity, 0);
    stats->movementKind = 0;
    for (int i = 0; i < 6; ++i) {
        SetBits(stats->valuesB0[i].base, kHundred);
        SetBits(stats->valuesB0[i].current, kHundred);
        SetBits(stats->valuesE0[i].base, kHundred);
        SetBits(stats->valuesE0[i].current, kHundred);
    }
    for (UnitStatSlot& slot : stats->slots) {
        slot.type = 1;
        slot.vitalMask = 0;
        slot.kind = kSlotWorking;
        SetBits(slot.current, kHundred);
        SetBits(slot.maximum, kHundred);
        for (UnitStatLayer& layer : slot.layers) {
            layer.layerMask = 0;
            layer.state = 0;
            layer.spareA = 0;
            layer.spareB = 0;
            std::memset(layer.absorb, 0, sizeof(layer.absorb));
        }
    }
    return 1;
}
EI_REPLACE_METHOD(0x005134F0, UnitStatsBuilder::SetDefaults);

int UnitStatsBuilder::BuildFromPrototype(UnitVitals* vitals, UnitStats* stats, const char* name) {
    const Prototype* prototype = FindPrototype(name);
    if (!prototype) {
        SetDefaults(vitals, stats);
        return 1;
    }
    const Model* model = ModelOf(prototype);
    const Npc* npc = FindNpc(name);
    if (!vitals || !stats) return 0;
    // Both blocks start zeroed: the fields the resets and this builder leave (the layers' spare
    // absorb values, the high bits of builtFlags, ...) are 0, not what the caller's stack held
    // (a determinism fix, SV7b3; the harness zeroes them in the original too, harness_leftovers.h).
    std::memset(vitals, 0, sizeof(UnitVitals));
    std::memset(stats, 0, sizeof(UnitStats));
    UnitVitalsReset(vitals);
    UnitStatsResetOriginal(stats);

    // Vitals: hit points and mana at their maxima, the model's regeneration.
    FpuCopy2(&vitals->maxHitPoints, &vitals->hitPoints, &prototype->hitPoints);
    CopyDword(vitals->hitPointRegen, model->hitPointRegen);
    FpuCopy2(&vitals->maxMana, &vitals->mana, &prototype->mana);
    CopyDword(vitals->manaRegen, model->manaRegen);
    std::memcpy(&stats->fixedMaxHitPoints, &vitals->maxHitPoints, 4);

    // The three stats, speed and rate.
    if (npc) {
        FpuCopy2(&stats->stat10Base, &stats->stat10, &npc->stats[0]);
        FpuCopy2(&stats->stat18Base, &stats->stat18, &npc->stats[1]);
        FpuCopy2(&stats->stat20Base, &stats->stat20, &npc->stats[2]);
        float rate = static_cast<float>(static_cast<double>(stats->stat18) * kHalf);
        CopyDword(stats->speedBase, prototype->speed);
        stats->rateBase = rate;
    } else {
        float strength = static_cast<float>(static_cast<double>(prototype->hitPoints) * kThird);
        stats->stat10Base = strength;
        stats->stat10 = strength;
        float dexterity = static_cast<float>(static_cast<double>(prototype->mana) * kThird);
        stats->stat18Base = dexterity;
        stats->stat18 = dexterity;
        SetBits(stats->stat20Base, kStat25);
        SetBits(stats->stat20, kStat25);
        SetBits(stats->rateBase, kRate15);
        CopyDword(stats->speedBase, prototype->speed);
    }
    CopyDword(stats->prototypeField12, prototype->field12);
    std::memcpy(stats->modeSpeeds, model->modeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        CopyDword(stats->valuesB0[i].base, prototype->valuesB0[i]);
        CopyDword(stats->valuesE0[i].base, prototype->valuesE0[i]);
    }
    stats->sightCone = HalfAngleCosine(model);
    CopyDword(stats->peripheralRange, prototype->peripheralRange);
    stats->sideCone = RightAngleCosine();
    CopyDword(stats->movementKind, model->movementKind);
    SetAttributesAndSkills(stats, prototype, npc);
    CopyDword(stats->modelField8, model->field8);
    stats->sizeClass = static_cast<std::int16_t>(model->sizeClass);
    auto small = static_cast<std::int16_t>(model->sizeClassSmall);
    stats->sizeClass20 = small;
    stats->sizeClass60 = small;

    // Protection: the model's values scaled by the prototype.
    for (int i = 0; i < 7; ++i) {
        float value = static_cast<float>(static_cast<double>(model->protection[i]) * prototype->protectionScale);
        vitals->protection[i].base = value;
        vitals->protection[i].current = value;
    }
    SetSlots(stats, model);
    SetCombat(stats, prototype, model);
    LearnNpcSkills(stats, npc);
    return 1;
}
EI_REPLACE_METHOD(0x005136D0, UnitStatsBuilder::BuildFromPrototype);

int __stdcall BuildNpcStats(UnitStats* stats, const char* name) {
    const Prototype* prototype = FindPrototype(name);
    const Model* model = ModelOf(prototype);   // reads the prototype before the null test
    const Npc* npc = FindNpc(name);
    if (!prototype) return game::FatalError(reinterpret_cast<const char*>(0x0078F288), name);
    if (!npc) game::DebugLog(reinterpret_cast<const char*>(0x0078F258), name);
    UnitStatsResetOriginal(stats);

    const float* speed = &prototype->speed;
    if (npc) {
        FpuCopy2(&stats->stat10Base, &stats->stat10, &npc->stats[0]);
        FpuCopy2(&stats->stat18Base, &stats->stat18, &npc->stats[1]);
        FpuCopy2(&stats->stat20Base, &stats->stat20, &npc->stats[2]);
        FpuCopy2(&stats->speedBase, &stats->speed, speed);
        float rate = static_cast<float>(static_cast<double>(stats->stat18) * kHalf);
        stats->rateBase = rate;
        stats->rate = rate;
    } else {
        float strength = static_cast<float>(static_cast<double>(prototype->hitPoints) * kThird);
        stats->stat10Base = strength;
        stats->stat10 = strength;
        float dexterity = static_cast<float>(static_cast<double>(prototype->mana) * kThird);
        stats->stat18Base = dexterity;
        stats->stat18 = dexterity;
        SetBits(stats->stat20Base, kStat25);
        SetBits(stats->stat20, kStat25);
        FpuCopy2(&stats->speedBase, &stats->speed, speed);
        SetBits(stats->rateBase, kRate15);
        SetBits(stats->rate, kRate15);
    }
    CopyDword(stats->prototypeField12, prototype->field12);
    std::memcpy(stats->modeSpeeds, model->modeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        FpuCopy2(&stats->valuesB0[i].base, &stats->valuesB0[i].current, &prototype->valuesB0[i]);
        FpuCopy2(&stats->valuesE0[i].base, &stats->valuesE0[i].current, &prototype->valuesE0[i]);
    }
    stats->sightCone = HalfAngleCosine(model);
    CopyDword(stats->peripheralRange, prototype->peripheralRange);
    stats->sideCone = RightAngleCosine();
    CopyDword(stats->movementKind, model->movementKind);
    SetAttributesAndSkills(stats, prototype, npc);
    CopyDword(stats->modelField8, model->field8);
    stats->sizeClass = static_cast<std::int16_t>(model->sizeClass);
    auto small = static_cast<std::int16_t>(model->sizeClassSmall);
    stats->sizeClass20 = small;
    stats->sizeClass60 = small;
    SetSlots(stats, model);
    SetCombat(stats, prototype, model);
    LearnNpcSkills(stats, npc);
    UnitStatsResetToBaseOriginal(stats, nullptr, 1.0f);
    UnitStatsRefillSlotsOriginal(stats);
    UnitStatsResetToBaseOriginal(stats, nullptr, 1.0f);
    return 1;
}
EI_REPLACE(0x00513DA0, BuildNpcStats);

int __stdcall PrototypeObjectClass(const char* name) {
    constexpr int kDefaultClass = 0x33;
    const Prototype* prototype = FindPrototype(name);
    if (!prototype) return kDefaultClass;
    const Model* model = ModelOf(prototype);
    if (!model) return kDefaultClass;
    return model->objectClass;
}
EI_REPLACE(0x00513240, PrototypeObjectClass);

// --- Statistics from their bases -----------------------------------------------------------------

namespace {
constexpr float kZero = 0.0f;                                     // 0073B858
constexpr float kOneF = 1.0f;                                     // 0073B84C
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);     // 0073C548 / 0073D09C (0.01)
constexpr float kTwelve = 12.0f;                                  // 0073E138
constexpr float kPointTwo = std::bit_cast<float>(0x3E4CCCCDu);    // 0073BAD4
constexpr float kTenF = 10.0f;                                    // 0073BAAC
constexpr std::uint32_t kMinusTwo = 0xC0000000u;
constexpr int kSkillBodyB0 = 15;
constexpr int kSkillHitPoints = 16;
constexpr int kSkillRate = 20;
constexpr int kSkillCapacity = 21;
constexpr int kSkillStat10 = 23;
constexpr int kSkillStat18 = 24;
constexpr int kSkillStat20 = 25;

float Global(std::uintptr_t address) { return *reinterpret_cast<const float*>(address); }

// 007C5460 ^ (ln(experience / 007C545C) / ln(007C5464)): FYL2X with FLDLN2 and the CRT's
// _CIpow (006E81A0), as the original computes it. Unrounded.
double HitPointExperienceCurve(const float* experience) {
    return libcrt::CiPow(Global(0x007C5460), x87::flog(static_cast<double>(*experience) / Global(0x007C545C)) / x87::flog(Global(0x007C5464)));
}

// A level record's value (+0x34, unsigned) as the original's FILD of a zero-extended qword.
double LevelOf(const char* record) {
    volatile std::int64_t wide = reinterpret_cast<const SkillLevelRecord*>(record)->values[7];
    return static_cast<double>(wide);
}
double LevelOrZero(UnitStats* stats, int skill) {
    const char* record = stats->SkillLevelRecord(skill);
    return record ? LevelOf(record) : kZero;
}

} // namespace

void UnitStats::ResetToBase(float newLoad) {
    std::memcpy(&stat10, &stat10Base, 4);
    std::memcpy(&stat18, &stat18Base, 4);
    std::memcpy(&stat20, &stat20Base, 4);
    for (int i = 0; i < 6; ++i) {
        std::memcpy(&valuesB0[i].current, &valuesB0[i].base, 4);
        std::memcpy(&valuesE0[i].current, &valuesE0[i].base, 4);
    }
    double bonus = kZero;
    if (const char* record = SkillLevelRecord(kSkillBodyB0)) {
        double level = LevelOf(record);
        if (level < kZero || level > kZero) bonus = level * valuesB0[1].current * kPercent;
    }
    valuesB0[1].current = static_cast<float>(bonus + valuesB0[1].current);
    stat10 = static_cast<float>(LevelOrZero(this, kSkillStat10) + stat10);
    stat18 = static_cast<float>(LevelOrZero(this, kSkillStat18) + stat18);
    std::memcpy(&sightCone, &kMinusTwo, 4);
    stat20 = static_cast<float>(LevelOrZero(this, kSkillStat20) + stat20);

    // The slot maxima from the hit point curve; current values clamped to them.
    double curve = HitPointExperienceCurve(&experience);
    float hitPoints = static_cast<float>(
        curve * (static_cast<double>(stat10) / Global(0x007C5454) * Global(0x007C5458)));
    float perShare = static_cast<float>(
        (LevelOrZero(this, kSkillHitPoints) * kPercent + kOneF) * hitPoints / slots[1].share);
    for (UnitStatSlot& slot : slots) {
        if (slot.kind <= 0) continue;
        float maximum = static_cast<float>(static_cast<double>(perShare) * slot.share);
        slot.maximum = maximum;
        std::uint32_t current = std::bit_cast<std::uint32_t>(slot.current);
        std::uint32_t chosen = !(maximum >= slot.current) ? std::bit_cast<std::uint32_t>(maximum) : current;
        slot.current = std::bit_cast<float>(chosen);
    }

    std::memcpy(&load, &newLoad, 4);   // a dword copy
    float strengthCapacity = static_cast<float>(static_cast<double>(stat10) * kTwelve);
    capacity = static_cast<float>((LevelOrZero(this, kSkillCapacity) * kPercent + kOneF) * strengthCapacity);
    float legs = LegsFactorAt(this, nullptr, 1);
    loadFactor = legs;
    speed = static_cast<float>(static_cast<double>(legs) * speedBase);
    armsFactor = ArmsFactorAt(this, nullptr, 1);
    rateBase = static_cast<float>(static_cast<double>(stat18) * kPointTwo + kTenF);
    rate = static_cast<float>((LevelOrZero(this, kSkillRate) * kPercent + kOneF) * armsFactor * rateBase);
}
EI_REPLACE_METHOD(0x00524CD0, UnitStats::ResetToBase);

namespace {
// 005259B0 UnitStats::Reduce and 00525800 UnitStats::HealSlots of unit_stats.cpp / unit_vitals.cpp,
// through their game addresses. Reduce leaves its result unrounded in ST0.
__declspec(naked) double __fastcall ReduceAt(UnitStats*, void*, float) EI_ORIGINAL(0x005259B0)
// 0051FF00. Skill database: the index of `record` in its level records, or -1. Not rewritten.

// The hit point maximum of the experience curve with skill 16 (the level byte read now).
float CurveHitPoints(UnitStats* stats) {
    double curve = HitPointExperienceCurve(&stats->experience);
    double scaled = curve * (static_cast<double>(stats->stat10) / Global(0x007C5454) * Global(0x007C5458));
    return static_cast<float>((LevelOrZero(stats, kSkillHitPoints) * kPercent + kOneF) * scaled);
}
} // namespace

void UnitStats::LearnSkill(const void* database, const void* record) {
    float oldMaximum = CurveHitPoints(this);
    float oldHitPoints = static_cast<float>(ReduceAt(this, nullptr, oldMaximum));
    int index = SkillRecordIndex(database, nullptr, record);
    int skill = index / 3;
    int level = index % 3;
    std::uint8_t& known = skillLevels[skill];   // unchecked, as the original
    if (known != 0 && level <= known - 1) return;
    known = static_cast<std::uint8_t>(level + 1);
    float newMaximum = CurveHitPoints(this);
    if (!(oldMaximum > kOneF)) return;   // NaN returns too
    if (!(newMaximum > kOneF)) return;
    double newHitPoints = ReduceAt(this, nullptr, newMaximum);
    float heal = static_cast<float>(static_cast<double>(oldHitPoints) / oldMaximum * newMaximum - newHitPoints);
    HealSlotsAt(this, nullptr, newMaximum, heal);
}
EI_REPLACE_METHOD(0x00526450, UnitStats::LearnSkill);

namespace {
constexpr float kStatBase = 25.0f;   // 0073E124
constexpr int kSkillRecordSize = 0x18;

// 00527050 UnitStats::SkillWithBonus of unit_stats.cpp, through its game address; ST0.
__declspec(naked) double __fastcall SkillWithBonusAt(UnitStats*, void*, std::uint32_t) EI_ORIGINAL(0x00527050)

// The level value of skill `index` stored to a float (FILD qword, FSTP float); 0 without one.
float LevelAsFloat(const UnitStats* stats, int index) {
    std::uint8_t level = stats->skillLevels[index];   // unchecked
    if (level == 0) return kZero;
    const char* record =
        unitdb::SkillLevels().RecordAt(static_cast<std::uint32_t>(index * 3 - 1 + level), unitdb::kSkillLevelSize);
    if (!record) return kZero;
    return static_cast<float>(LevelOf(record));
}

// The skill bonus byte (skill 0 or 1) of the unit's weapon in hand, or 0.
float WeaponBonus(CUnit* unit, int skill) {
    CEngineObject* weapon = unit->GetWeapon(unit->activeWeapon);
    if (!weapon) return kZero;
    const auto* record = reinterpret_cast<const itemdb::WeaponRecord*>(static_cast<CItem*>(weapon)->GetPrototype());
    return static_cast<float>(static_cast<int>(record->skillBonus[skill]));
}
} // namespace

double UnitStats::WeaponSkillBonus(int type, CUnit* unit) {
    if (type < 5 || type == 5 || type == 6) {
        int skill = type >= 5 ? 1 : 0;
        float level = LevelAsFloat(this, type);
        if (!(IsTextKey(unit->id_) & 0xFF)) return static_cast<double>(static_cast<int>(skills[skill])) + level;
        float weapon = WeaponBonus(unit, skill);
        float value = static_cast<float>(static_cast<int>(skills[skill]));
        const auto* record = reinterpret_cast<const SkillRecord*>(
            unitdb::Skills().RecordAt(static_cast<std::uint32_t>(skill), kSkillRecordSize));
        char stat = static_cast<const char*>(record->statNames.begin()[0])[0];   // its first stat's letter
        double base;
        if (stat == 's') base = static_cast<double>(stat10) - kStatBase;
        else if (stat == 'i') base = static_cast<double>(stat20) - kStatBase;
        else if (stat == 'd') base = static_cast<double>(stat18) - kStatBase;
        else return (static_cast<double>(value) + weapon) + level;
        return ((base + value) + weapon) + level;
    }
    if (type == 8) {
        if (!(IsTextKey(unit->id_) & 0xFF)) return static_cast<double>(static_cast<int>(skills[0]));
        float weapon = WeaponBonus(unit, 0);
        return SkillWithBonusAt(this, nullptr, 0) + weapon;
    }
    return kZero;
}
EI_REPLACE_METHOD(0x00526C70, UnitStats::WeaponSkillBonus);

namespace {
// 0051FE80. Skill database: the index of the level record named `name` by _mbsicmp, or -1.
int __fastcall SkillRecordIndexByName(const void* a0, void*, const char* a2) { return SkillIndexOfOriginal(a0, nullptr, a2); }

// A CString field of a record: is it set, and not "none"?
bool NamesRequirement(const mfc::CString& text) {
    if (text.GetLength() == 0) return false;
    return gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(text)),
                           reinterpret_cast<const unsigned char*>(0x007846F0)) != 0;   // "none"
}
const ::SkillLevelRecord* LevelRecordAt(std::uint32_t index) {
    return reinterpret_cast<const ::SkillLevelRecord*>(unitdb::SkillLevels().RecordAt(index, unitdb::kSkillLevelSize));
}
} // namespace

BOOL UnitStats::CanLearn(const void* database, const char* recordBytes) {
    const auto* record = reinterpret_cast<const ::SkillLevelRecord*>(recordBytes);
    if (!record->learnable) return 0;
    if (!(stat10 >= record->RequiredStat(0))) return 0;   // NaN fails too
    if (!(stat18 >= record->RequiredStat(1))) return 0;
    if (!(stat20 >= record->RequiredStat(2))) return 0;
    if (NamesRequirement(record->requiredSkillName)) {
        int skill = record->RequiredSkill();
        if (skill == 0x7FFFFFFF) return 0;
        if (static_cast<std::uint32_t>(skills[skill]) < record->RequiredSkillValue()) return 0;   // unchecked
    }
    if (NamesRequirement(record->requiredLevelName)) {
        int required = SkillRecordIndexByName(database, nullptr, record->requiredLevelName);
        if (skillLevels[required / 3] != static_cast<std::uint32_t>(required % 3 + 1)) return 0;
    }
    // No level of the same skill id may be known in another skill.
    int group = SkillRecordIndex(database, nullptr, record) / 3;
    for (int k = 0; k < 26; ++k) {
        std::uint8_t level = skillLevels[k];
        if (level == 0) continue;
        const ::SkillLevelRecord* known = LevelRecordAt(static_cast<std::uint32_t>(3 * k + level - 1));
        if (!known || known->exclusionGroup != record->exclusionGroup) continue;
        if (SkillRecordIndex(database, nullptr, known) / 3 != group) return 0;
    }
    int own = SkillRecordIndexByName(database, nullptr, record->name);
    return own % 3 > static_cast<int>(skillLevels[own / 3]) - 1 ? 1 : 0;
}
EI_REPLACE_METHOD(0x005260A0, UnitStats::CanLearn);

// --- Applying a prototype ---------------------------------------------------------------------

int UnitStatsBuilder::ApplyPrototype(CEngineObject* object, const char* name) {
    UnitVitals vitals{};   // zeroed (the original's start as the stack held them; SV7b3)
    UnitStats stats{};
    auto* mapObject = static_cast<CMapObject*>(object);
    const Prototype* prototype = FindPrototype(name);
    if (!prototype) {
        UnitVitalsReset(&vitals);
        if (object->GetClassId() != kClassUnit) {
            SetDefaults(&vitals, nullptr);
            mapObject->SetServerData(&vitals);
        } else {
            UnitStatsResetOriginal(&stats);
            SetDefaults(&vitals, &stats);
            mapObject->SetStats(&stats);
        }
        mapObject->ScaleEffectChanged(0);
        object->prototypeId_ = 0;
        game::DebugLog(reinterpret_cast<const char*>(0x0078F22C), name, object->id_);
        return 1;
    }
    const Model* model = ModelOf(prototype);
    if (object->GetClassId() == kClassUnit) {
        UnitStatsBuildFromPrototype(this, nullptr, &vitals, &stats, name);
        stats.builtFlags |= 1;
        vitals.builtFlags |= 1;
        SetServerDataAt(object, nullptr, &vitals);
        mapObject->SetStats(&stats);
        // The figure stands lower by the model's height.
        float lift = static_cast<float>(-static_cast<double>(model->height));
        CObject3D* figure = static_cast<CObject3D*>(object);
        SetBits(figure->defaultScale.x, 0);
        figure->defaultScale.y = lift;
        SetBits(figure->defaultScale.z, 0);
        Vector3 scale;
        static_assert(sizeof(scale) == sizeof(prototype->scale));
        std::memcpy(&scale, &prototype->scale, sizeof(scale));
        ObjectSetFigureScaleOriginal(object, nullptr, &scale.x);
        std::memcpy(&mapObject->baseScale, &scale, sizeof(scale));
        mapObject->ApplyBaseScale();
        SetPrototype23CAt(object, nullptr, reinterpret_cast<const char*>(prototype));
    }
    char* record = UnitPrototypeFindCopy(&unitdb::PrototypeTable(), nullptr, name);
    object->prototypeId_ = record
        ? static_cast<std::uint32_t>(static_cast<int>(record - unitdb::PrototypeTable().begin) / unitdb::kPrototypeSize)
        : 0xFFFFFFFFu;
    return 1;
}
EI_REPLACE_METHOD(0x00513B60, UnitStatsBuilder::ApplyPrototype);

// --- The factory ------------------------------------------------------------------------------

namespace {
// The constructors (thiscall on the new block; each returns it).
using Constructor = CEngineObject*(__fastcall*)(void*);
struct FactoryEntry {
    int classId;
    std::size_t size;
    std::uintptr_t constructor;
};
constexpr FactoryEntry kFactory[] = {
    {0x32, 0x4DC, 0x0052E110},   // units (CUnit)
    {0x33, 0x4DC, 0x0052E110},
    {0x34, 0x4DC, 0x0052E110},
    {0x35, 0x238, 0x005296E0},
    {0x36, 0x238, 0x005297C0},
    {0x37, 0x238, 0x005298A0},
    {0x38, 0x238, 0x00529980},
    {0x39, 0x580, 0x004466D0},
    {0x3A, 0x23C, 0x0052BEE0},
    {0x3B, 0x290, 0x00529DD0},
    {0x3C, 0x264, 0x0052B1F0},
    {0x300D, 0x78, 0x0050C5F0},
    {0x3010, 0x23C, 0x00529A80},
};
} // namespace

// Every object starts zeroed (operator_new_zeroed): the units' constructors leave levels4C8 and the
// motion's turn fields, which the saves write (SV7b2).
CEngineObject* CMapObjectManager::CreateObject(int classId) {
    for (const FactoryEntry& entry : kFactory) {
        if (entry.classId != classId) continue;
        void* block = gamecrt::operator_new_zeroed(entry.size);
        if (!block) return nullptr;
        return reinterpret_cast<Constructor>(entry.constructor)(block);
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x00512730, CMapObjectManager::CreateObject);
