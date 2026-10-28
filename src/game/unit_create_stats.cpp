// A creature's starting vitals and statistics from its prototype records, the prototype applied
// to a new map object, and the map object factory. See unit_create.h and docs/notes/units.md ("Creation,
// databases, save").
#include "unit_create.h"

#include "base_manager.h"
#include "item.h"
#include "lever.h"
#include "magic_trap.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_kinds.h"
#include "object_tables.h"
#include "torch.h"
#include "trap_object.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/crt.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "logic_settings.h"

#include <bit>
#include <cstring>
#include <string>

// Values kept on the FPU stack are double locals; stored values are floats.

namespace {

constexpr float kThird = std::bit_cast<float>(0x3EAAAAABu);
constexpr float kHalf = 0.5f;
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

// pi and 1/180 and 90: volatile so the products are computed at run time with the FPU's precision.
volatile double g_pi = 3.141592653589793;
volatile double g_perDegree = std::bit_cast<double>(0x3F76C16C16C16C17ull);
volatile double g_ninety = 90.0;

// These are stored as dwords (bit patterns), not through the FPU.
void SetBits(float& field, std::uint32_t bits) { std::memcpy(&field, &bits, sizeof(field)); }
template <typename To, typename From>
void CopyDword(To& to, const From& from) {
    static_assert(sizeof(To) == 4 && sizeof(From) == 4);
    std::memcpy(&to, &from, 4);
}

// FLD [src]; FST [a]; FSTP [b]: a float copied through the FPU to two places.
void FpuCopy2(float* a, float* b, const float* src) {
    std::memcpy(a, src, sizeof(float));
    std::memcpy(b, src, sizeof(float));
}

std::uint8_t RoundToByte(float value) { return static_cast<std::uint8_t>(x87::fistp(value)); }

const UnitPrototypeRecord* FindPrototype(std::string_view name) { return g_unitDatabase->prototypes.FindByName(name); }
const UnitNpcRecord* FindNpc(const char* name) { return g_unitDatabase->npcs.FindByName(name); }

// The prototype's model; its index is read even from a null prototype (that faults).
const UnitModelRecord* ModelOf(const UnitPrototypeRecord* prototype) {
    auto index = static_cast<std::uint32_t>(static_cast<const volatile UnitPrototypeRecord*>(prototype)->modelIndex);
    return unitdb::ModelRecord(index);
}

// --- The statistics both builders fill the same way ---------------------------------------

// cos(angle / 2) with the angle in degrees, stored to a float.
float HalfAngleCosine(const UnitModelRecord* model) {
    double half = static_cast<double>(model->angle) * kHalf;
    double perDegree = g_pi * g_perDegree;
    return static_cast<float>(x87::fcos(half * perDegree));
}

float RightAngleCosine() {
    double perDegree = g_pi * g_perDegree;
    return static_cast<float>(x87::fcos(perDegree * g_ninety));
}

void SetAttributesAndSkills(UnitStats* stats, const UnitPrototypeRecord* prototype, const UnitNpcRecord* npc) {
    if (npc) {
        static_assert(offsetof(UnitNpcRecord, skills) - offsetof(UnitNpcRecord, attributes) == 3);
        std::memcpy(stats->attributes, npc->attributes, 3 + 8);   // the attributes and the skills
        return;
    }
    for (int i = 0; i < 3; ++i) stats->attributes[i] = RoundToByte(prototype->skillLevel);
    for (int i = 0; i < 8; ++i) stats->skills[i] = RoundToByte(prototype->skillLevel);
    stats->attributes[0] = RoundToByte(prototype->attributes01);
    stats->attributes[1] = RoundToByte(prototype->attributes01);
    stats->attributes[2] = RoundToByte(prototype->attribute2);
    for (int i = 0; i < 3; ++i) stats->skills[3 + i] = static_cast<std::uint8_t>(libcrt::Ftol(prototype->skills3to5[i]));
}

// The body slots from the model: a slot with a share is working, with its type and vital mask
// from its level record and no layers; the others unused.
void SetSlots(UnitStats* stats, const UnitModelRecord* model) {
    for (int i = 0; i < 6; ++i) {
        const UnitModelSlot& entry = model->slots[i];
        UnitStatSlot& slot = stats->slots[i];
        float share = entry.share;
        EI_NAN_AUDIT(share);
        if (share == 0.0f) {
            slot.kind = 0;
            continue;
        }
        const UnitLevelRecord* level = unitdb::ModelLevelAt(static_cast<std::uint32_t>(entry.levelIndex));
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

void SetCombat(UnitStats* stats, const UnitPrototypeRecord* prototype, const UnitModelRecord* model) {
    for (int i = 0; i < 5; ++i) CopyDword(stats->combat[i], prototype->combat[i]);
    CopyDword(stats->damage, prototype->damage);
    CopyDword(stats->damageSpread, prototype->damageSpread);
    static_assert(sizeof(stats->damageWeights) == sizeof(model->damageWeights));
    std::memcpy(stats->damageWeights, model->damageWeights, sizeof(stats->damageWeights));
    CopyDword(stats->naturalWeapon, prototype->naturalWeapon);
}

void LearnNpcSkills(UnitStats* stats, const UnitNpcRecord* npc) {
    stats->spentExperience = 0.0f;
    if (!npc) return;
    stats->AddScaledExperience(npc->experience);
    for (const std::string* name = npc->skillNames.data(); name != npc->skillNames.data() + npc->skillNames.size(); ++name) {
        ::SkillLevelRecord* skill = g_skillDatabase->FindLevel(name->c_str());
        if (skill) stats->LearnSkill(&g_skillDatabase.Get(), skill);
    }
    std::memcpy(&stats->spentExperience, &stats->experience, 4);
}

constexpr float kZero = 0.0f;
constexpr float kOneF = 1.0f;
constexpr float kPercent = std::bit_cast<float>(0x3C23D70Au);     // (0.01)
constexpr float kTwelve = 12.0f;
constexpr float kPointTwo = std::bit_cast<float>(0x3E4CCCCDu);
constexpr float kTenF = 10.0f;
constexpr std::uint32_t kMinusTwo = 0xC0000000u;
constexpr int kSkillBodyB0 = 15;
constexpr int kSkillHitPoints = 16;
constexpr int kSkillRate = 20;
constexpr int kSkillCapacity = 21;
constexpr int kSkillStat10 = 23;
constexpr int kSkillStat18 = 24;
constexpr int kSkillStat20 = 25;

// hpVal[3] ^ (ln(experience / hpVal[2]) / ln(hpVal[4])) with the CRT's _CIpow. Unrounded.
double HitPointExperienceCurve(const float* experience) {
    return libcrt::CiPow(g_logicSettings.hpVal[3], x87::flog(static_cast<double>(*experience) / g_logicSettings.hpVal[2]) / x87::flog(g_logicSettings.hpVal[4]));
}

// A level record's value (unsigned; exact).
double LevelOf(const SkillLevelRecord* record) {
    volatile std::int64_t wide = record->values[7];
    return static_cast<double>(wide);
}
double LevelOrZero(UnitStats* stats, int skill) {
    const SkillLevelRecord* record = stats->SkillLevelRecord(skill);
    return record ? LevelOf(record) : kZero;
}

// The hit point maximum of the experience curve with skill 16 (the level byte read now).
float CurveHitPoints(UnitStats* stats) {
    double curve = HitPointExperienceCurve(&stats->experience);
    double scaled = curve * (static_cast<double>(stats->strength) / g_logicSettings.hpVal[0] * g_logicSettings.hpVal[1]);
    return static_cast<float>((LevelOrZero(stats, kSkillHitPoints) * kPercent + kOneF) * scaled);
}

constexpr float kStatBase = 25.0f;

// The level value of skill `index` stored to a float; 0 without one.
float LevelAsFloat(const UnitStats* stats, int index) {
    std::uint8_t level = stats->skillLevels[index];   // unchecked
    if (level == 0) return kZero;
    const SkillLevelRecord* record = unitdb::SkillLevelAt(static_cast<std::uint32_t>(index * 3 - 1 + level));
    if (!record) return kZero;
    return static_cast<float>(LevelOf(record));
}

float WeaponBonus(CUnit* unit, int skill) {
    CItem* weapon = unit->GetWeapon(unit->activeWeapon);
    if (!weapon) return kZero;
    const auto* record = itemdb::RecordAs<itemdb::WeaponRecord>(weapon->GetPrototype());
    return static_cast<float>(static_cast<int>(record->skillBonus[skill]));
}

bool NamesRequirement(const std::string& text) {
    if (text.size() == 0) return false;
    return str::Compare(text,
                           "none") != 0;
}
const ::SkillLevelRecord* LevelRecordAt(std::uint32_t index) { return unitdb::SkillLevelAt(index); }

struct FactoryEntry {
    int classId;
    CEngineObject* (*create)();
};
// The class's entry: its id and a new object of it.
template <class T>
constexpr FactoryEntry Make(int classId) {
    return {classId, [] () -> CEngineObject* { return new (kZeroed) T(); }};
}
constexpr FactoryEntry kFactory[] = {
    Make<CUnit>(0x32),
    Make<CUnit>(0x33),
    Make<CUnit>(0x34),
    Make<CTree>(0x35),
    Make<CBuilding>(0x36),
    Make<CStone>(0x37),
    Make<CStuffObject>(0x38),
    Make<CTorchObject>(0x3A),
    Make<MagicTrap>(0x3B),
    Make<Lever>(0x3C),
    Make<CFigureAnimationState>(0x300D),
    Make<CTrapObject>(0x3010),
};

} // namespace

// --- Name lookups -----------------------------------------------------------------------------

UnitNpcRecord* NpcPrototypeTable::FindByName(std::string_view name) {
    std::string key(name);
    std::string pairKey(key);
    UnitNpcRecord* record = byName[pairKey];
    return record;
}

// --- Builders ---------------------------------------------------------------------------------

int CMapObjectManager::SetDefaults(UnitVitals* vitals, UnitStats* stats) {
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
    SetBits(stats->strengthBase, kDefaultStat);
    SetBits(stats->dexterityBase, kDefaultStat);
    SetBits(stats->intelligenceBase, kDefaultStat);
    for (float& modeSpeed : stats->modeSpeeds) SetBits(modeSpeed, kDefaultModeSpeed);
    SetBits(stats->spentExperience, 0);
    SetBits(stats->speedBase, kDefaultSpeed);
    SetBits(stats->rateBase, kOne);
    SetBits(stats->capacity, 0);
    stats->movementKind = 0;
    for (int i = 0; i < 6; ++i) {
        SetBits(stats->perception[i].base, kHundred);
        SetBits(stats->perception[i].current, kHundred);
        SetBits(stats->conspicuity[i].base, kHundred);
        SetBits(stats->conspicuity[i].current, kHundred);
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
            layer.reserved[0] = 0;
            layer.reserved[1] = 0;
            std::memset(layer.absorb, 0, sizeof(layer.absorb));
        }
    }
    return 1;
}

int CMapObjectManager::BuildFromPrototype(UnitVitals* vitals, UnitStats* stats, const char* name) {
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) {
        SetDefaults(vitals, stats);
        return 1;
    }
    const UnitModelRecord* model = ModelOf(prototype);
    const UnitNpcRecord* npc = FindNpc(name);
    if (!vitals || !stats) return 0;
    // Both blocks start zeroed: the fields the resets and this builder leave (the layers' spare absorb
    // values, the high bits of builtFlags, ...) are 0, not what the caller's stack held (a determinism fix;
    // the harness zeroes them in the original too).
    std::memset(vitals, 0, sizeof(UnitVitals));
    std::memset(stats, 0, sizeof(UnitStats));
    vitals->Reset();
    stats->Reset();

    FpuCopy2(&vitals->maxHitPoints, &vitals->hitPoints, &prototype->hitPoints);
    CopyDword(vitals->hitPointRegen, model->hitPointRegen);
    FpuCopy2(&vitals->maxMana, &vitals->mana, &prototype->mana);
    CopyDword(vitals->manaRegen, model->manaRegen);
    std::memcpy(&stats->fixedMaxHitPoints, &vitals->maxHitPoints, 4);

    if (npc) {
        FpuCopy2(&stats->strengthBase, &stats->strength, &npc->stats[0]);
        FpuCopy2(&stats->dexterityBase, &stats->dexterity, &npc->stats[1]);
        FpuCopy2(&stats->intelligenceBase, &stats->intelligence, &npc->stats[2]);
        float rate = static_cast<float>(static_cast<double>(stats->dexterity) * kHalf);
        CopyDword(stats->speedBase, prototype->speed);
        stats->rateBase = rate;
    } else {
        float strength = static_cast<float>(static_cast<double>(prototype->hitPoints) * kThird);
        stats->strengthBase = strength;
        stats->strength = strength;
        float dexterity = static_cast<float>(static_cast<double>(prototype->mana) * kThird);
        stats->dexterityBase = dexterity;
        stats->dexterity = dexterity;
        SetBits(stats->intelligenceBase, kStat25);
        SetBits(stats->intelligence, kStat25);
        SetBits(stats->rateBase, kRate15);
        CopyDword(stats->speedBase, prototype->speed);
    }
    CopyDword(stats->attackCategory, prototype->attackCategory);
    std::memcpy(stats->modeSpeeds, model->modeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        CopyDword(stats->perception[i].base, prototype->perception[i]);
        CopyDword(stats->conspicuity[i].base, prototype->conspicuity[i]);
    }
    stats->sightCone = HalfAngleCosine(model);
    CopyDword(stats->peripheralRange, prototype->peripheralRange);
    stats->sideCone = RightAngleCosine();
    CopyDword(stats->movementKind, model->movementKind);
    SetAttributesAndSkills(stats, prototype, npc);
    CopyDword(stats->reachHeightScale, model->reachHeightScale);
    stats->sizeClass = static_cast<std::int16_t>(model->sizeClass);
    auto smallClass = static_cast<std::int16_t>(model->sizeClassSmall);
    stats->sizeClass20 = smallClass;
    stats->sizeClass60 = smallClass;

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

int BuildNpcStats(UnitStats* stats, const char* name) {
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    const UnitModelRecord* model = ModelOf(prototype);   // reads the prototype before the null test
    const UnitNpcRecord* npc = FindNpc(name);
    if (!prototype) return game::FatalError("Can't find prototype \"%s\" for new HERO.\n", name);
    if (!npc) game::DebugLog("Can't find NPC prototype \"%s\" for new HERO.\n", name);
    stats->Reset();

    const float* speed = &prototype->speed;
    if (npc) {
        FpuCopy2(&stats->strengthBase, &stats->strength, &npc->stats[0]);
        FpuCopy2(&stats->dexterityBase, &stats->dexterity, &npc->stats[1]);
        FpuCopy2(&stats->intelligenceBase, &stats->intelligence, &npc->stats[2]);
        FpuCopy2(&stats->speedBase, &stats->speed, speed);
        float rate = static_cast<float>(static_cast<double>(stats->dexterity) * kHalf);
        stats->rateBase = rate;
        stats->rate = rate;
    } else {
        float strength = static_cast<float>(static_cast<double>(prototype->hitPoints) * kThird);
        stats->strengthBase = strength;
        stats->strength = strength;
        float dexterity = static_cast<float>(static_cast<double>(prototype->mana) * kThird);
        stats->dexterityBase = dexterity;
        stats->dexterity = dexterity;
        SetBits(stats->intelligenceBase, kStat25);
        SetBits(stats->intelligence, kStat25);
        FpuCopy2(&stats->speedBase, &stats->speed, speed);
        SetBits(stats->rateBase, kRate15);
        SetBits(stats->rate, kRate15);
    }
    CopyDword(stats->attackCategory, prototype->attackCategory);
    std::memcpy(stats->modeSpeeds, model->modeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        FpuCopy2(&stats->perception[i].base, &stats->perception[i].current, &prototype->perception[i]);
        FpuCopy2(&stats->conspicuity[i].base, &stats->conspicuity[i].current, &prototype->conspicuity[i]);
    }
    stats->sightCone = HalfAngleCosine(model);
    CopyDword(stats->peripheralRange, prototype->peripheralRange);
    stats->sideCone = RightAngleCosine();
    CopyDword(stats->movementKind, model->movementKind);
    SetAttributesAndSkills(stats, prototype, npc);
    CopyDword(stats->reachHeightScale, model->reachHeightScale);
    stats->sizeClass = static_cast<std::int16_t>(model->sizeClass);
    auto smallClass = static_cast<std::int16_t>(model->sizeClassSmall);
    stats->sizeClass20 = smallClass;
    stats->sizeClass60 = smallClass;
    SetSlots(stats, model);
    SetCombat(stats, prototype, model);
    LearnNpcSkills(stats, npc);
    stats->ResetToBase(1.0f);
    stats->RefillSlots();
    stats->ResetToBase(1.0f);
    return 1;
}

int PrototypeObjectClass(std::string_view name) {
    constexpr int kDefaultClass = 0x33;
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) return kDefaultClass;
    const UnitModelRecord* model = ModelOf(prototype);
    if (!model) return kDefaultClass;
    return model->objectClass;
}

// --- Statistics from their bases -----------------------------------------------------------------

void UnitStats::ResetToBase(float newLoad) {
    std::memcpy(&strength, &strengthBase, 4);
    std::memcpy(&dexterity, &dexterityBase, 4);
    std::memcpy(&intelligence, &intelligenceBase, 4);
    for (int i = 0; i < 6; ++i) {
        std::memcpy(&perception[i].current, &perception[i].base, 4);
        std::memcpy(&conspicuity[i].current, &conspicuity[i].base, 4);
    }
    double bonus = kZero;
    if (const ::SkillLevelRecord* record = SkillLevelRecord(kSkillBodyB0)) {
        double level = LevelOf(record);
        EI_NAN_AUDIT(level);
        if (level != kZero) bonus = level * perception[1].current * kPercent;
    }
    perception[1].current = static_cast<float>(bonus + perception[1].current);
    strength = static_cast<float>(LevelOrZero(this, kSkillStat10) + strength);
    dexterity = static_cast<float>(LevelOrZero(this, kSkillStat18) + dexterity);
    std::memcpy(&sightCone, &kMinusTwo, 4);
    intelligence = static_cast<float>(LevelOrZero(this, kSkillStat20) + intelligence);

    double curve = HitPointExperienceCurve(&experience);
    float hitPoints = static_cast<float>(
        curve * (static_cast<double>(strength) / g_logicSettings.hpVal[0] * g_logicSettings.hpVal[1]));
    float perShare = static_cast<float>(
        (LevelOrZero(this, kSkillHitPoints) * kPercent + kOneF) * hitPoints / slots[1].share);
    for (UnitStatSlot& slot : slots) {
        if (slot.kind <= 0) continue;
        float maximum = static_cast<float>(static_cast<double>(perShare) * slot.share);
        slot.maximum = maximum;
        std::uint32_t current = std::bit_cast<std::uint32_t>(slot.current);
        std::uint32_t chosen = maximum >= slot.current ? current : std::bit_cast<std::uint32_t>(maximum);
        slot.current = std::bit_cast<float>(chosen);
    }

    std::memcpy(&load, &newLoad, 4);   // a dword copy
    float strengthCapacity = static_cast<float>(static_cast<double>(strength) * kTwelve);
    capacity = static_cast<float>((LevelOrZero(this, kSkillCapacity) * kPercent + kOneF) * strengthCapacity);
    float legs = LegsFactor(1);
    loadFactor = legs;
    speed = static_cast<float>(static_cast<double>(legs) * speedBase);
    armsFactor = ArmsFactor(1);
    rateBase = static_cast<float>(static_cast<double>(dexterity) * kPointTwo + kTenF);
    rate = static_cast<float>((LevelOrZero(this, kSkillRate) * kPercent + kOneF) * armsFactor * rateBase);
}

void UnitStats::LearnSkill(const SkillDatabase* database, const ::SkillLevelRecord* record) {
    float oldMaximum = CurveHitPoints(this);
    float oldHitPoints = static_cast<float>(Reduce(oldMaximum));
    int index = const_cast<SkillDatabase*>(database)->IndexOfLevel(record);
    int skill = index / 3;
    int level = index % 3;
    std::uint8_t& known = skillLevels[skill];   // unchecked
    if (known != 0 && level <= known - 1) return;
    known = static_cast<std::uint8_t>(level + 1);
    float newMaximum = CurveHitPoints(this);
    EI_NAN_AUDIT(oldMaximum, newMaximum);
    if (oldMaximum <= kOneF) return;
    if (newMaximum <= kOneF) return;
    double newHitPoints = Reduce(newMaximum);
    float heal = static_cast<float>(static_cast<double>(oldHitPoints) / oldMaximum * newMaximum - newHitPoints);
    HealSlots(newMaximum, heal);
}

double UnitStats::WeaponSkillBonus(int type, CUnit* unit) {
    if (type < 5 || type == 5 || type == 6) {
        int skill = type >= 5 ? 1 : 0;
        float level = LevelAsFloat(this, type);
        if (!(IsTextKey(unit->id_) & 0xFF)) return static_cast<double>(static_cast<int>(skills[skill])) + level;
        float weapon = WeaponBonus(unit, skill);
        float value = static_cast<float>(static_cast<int>(skills[skill]));
        const SkillRecord* record = unitdb::SkillAt(static_cast<std::uint32_t>(skill));
        char stat = record->statNames.begin()[0][0];   // its first stat's letter
        double base;
        if (stat == 's') base = static_cast<double>(strength) - kStatBase;
        else if (stat == 'i') base = static_cast<double>(intelligence) - kStatBase;
        else if (stat == 'd') base = static_cast<double>(dexterity) - kStatBase;
        else return (static_cast<double>(value) + weapon) + level;
        return ((base + value) + weapon) + level;
    }
    if (type == 8) {
        if (!(IsTextKey(unit->id_) & 0xFF)) return static_cast<double>(static_cast<int>(skills[0]));
        float weapon = WeaponBonus(unit, 0);
        return SkillWithBonus(0) + weapon;
    }
    return kZero;
}

BOOL UnitStats::CanLearn(const SkillDatabase* database, const ::SkillLevelRecord* record) {
    if (!record->learnable) return 0;
    EI_NAN_AUDIT(strength, dexterity, intelligence);
    if (strength < record->RequiredStat(0)) return 0;
    if (dexterity < record->RequiredStat(1)) return 0;
    if (intelligence < record->RequiredStat(2)) return 0;
    if (NamesRequirement(record->requiredSkillName)) {
        int skill = record->RequiredSkill();
        if (skill == 0x7FFFFFFF) return 0;
        if (static_cast<std::uint32_t>(skills[skill]) < record->RequiredSkillValue()) return 0;   // unchecked
    }
    if (NamesRequirement(record->requiredLevelName)) {
        int required = database->IndexOf(record->requiredLevelName);
        if (skillLevels[required / 3] != static_cast<std::uint32_t>(required % 3 + 1)) return 0;
    }
    int group = const_cast<SkillDatabase*>(database)->IndexOfLevel(record) / 3;
    for (int k = 0; k < 26; ++k) {
        std::uint8_t level = skillLevels[k];
        if (level == 0) continue;
        const ::SkillLevelRecord* known = LevelRecordAt(static_cast<std::uint32_t>(3 * k + level - 1));
        if (!known || known->exclusionGroup != record->exclusionGroup) continue;
        if (const_cast<SkillDatabase*>(database)->IndexOfLevel(known) / 3 != group) return 0;
    }
    int own = database->IndexOf(record->name);
    return own % 3 > static_cast<int>(skillLevels[own / 3]) - 1 ? 1 : 0;
}

int CMapObjectManager::ApplyPrototype(CMapObject* object, const char* name) {
    UnitVitals vitals{};   // zeroed (the stack's old contents are not kept)
    UnitStats stats{};
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) {
        vitals.Reset();
        if (object->GetClassId() != kClassUnit) {
            SetDefaults(&vitals, nullptr);
            object->SetServerData(&vitals);
        } else {
            stats.Reset();
            SetDefaults(&vitals, &stats);
            object->SetStats(&stats);
        }
        object->ScaleEffectChanged(0);
        object->prototypeId_ = 0;
        game::DebugLog("Can't find prototype \"%s\" for object %d.\n", name, object->id_);
        return 1;
    }
    const UnitModelRecord* model = ModelOf(prototype);
    if (object->GetClassId() == kClassUnit) {
        BuildFromPrototype(&vitals, &stats, name);
        stats.builtFlags |= 1;
        vitals.builtFlags |= 1;
        object->CMapObject::SetServerData(&vitals);
        object->SetStats(&stats);
        float lift = static_cast<float>(-static_cast<double>(model->height));
        CObject3D* figure = object;
        SetBits(figure->defaultScale.x, 0);
        figure->defaultScale.y = lift;
        SetBits(figure->defaultScale.z, 0);
        Vector3 scale;
        static_assert(sizeof(scale) == sizeof(prototype->scale));
        std::memcpy(&scale, &prototype->scale, sizeof(scale));
        object->SetFigureScale(&scale.x);
        std::memcpy(&object->baseScale, &scale, sizeof(scale));
        object->ApplyBaseScale();
        UnitOrNull(object)->SetPrototype23C(prototype);
    }
    UnitPrototypeRecord* record = g_unitDatabase->prototypes.FindByName(name);
    object->prototypeId_ = record ? static_cast<std::uint32_t>(record - g_unitDatabase->prototypes.records.data()) : 0xFFFFFFFFu;
    return 1;
}

// --- The factory ------------------------------------------------------------------------------

// Every object starts zeroed (kZeroed): the units' constructors leave levels4C8 and the
// motion's turn fields, which the saves write.
CEngineObject* CMapObjectManager::NewObject(int classId) {
    for (const FactoryEntry& entry : kFactory) {
        if (entry.classId != classId) continue;
        return entry.create();
    }
    return nullptr;
}
