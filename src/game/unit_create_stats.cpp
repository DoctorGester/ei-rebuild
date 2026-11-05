// A creature's starting vitals and statistics from its prototype records, the prototype applied
// to a new map object, and the map object factory. See unit_create.h and docs/notes/units.md ("Creation,
// databases, save").
#include "game/angles.h"
#include "game/base_manager.h"
#include "game/item.h"
#include "game/lever.h"
#include "game/logic_settings.h"
#include "game/magic_trap.h"
#include "game/map_object.h"
#include "game/map_object_figures.h"
#include "game/map_object_kinds.h"
#include "game/text_manager.h"
#include "game/torch.h"
#include "game/trap_object.h"
#include "game/unit.h"
#include "game/unit_create.h"
#include "game/unit_db.h"

#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/rounding.h"
#include "lib/str.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

constexpr float kThird = 0.33333334f;
constexpr float kStat25 = 25.0f;
constexpr float kRate15 = 15.0f;
constexpr float kDefaultStat = 50.0f;
constexpr float kDefaultModeSpeed = 0.13333334f;
constexpr float kDefaultSpeed = 0.2f;
constexpr float kDefaultRateBase = 1.0f;
constexpr float kDefaultFullLevel = 100.0f;   // protection, perception, conspicuity and a slot's capacity at their start
constexpr float kDefaultVitalPool = 1000.0f;   // hit points and mana of a unit without stats
constexpr float kDefaultRegen = 10.0f;
constexpr int kSlotWorking = 3;


// A dword copied as its bits.
template <typename To, typename From>
void CopyDword(To& to, const From& from) {
    to = std::bit_cast<To>(from);
}

std::uint8_t RoundToByte(float value) { return static_cast<std::uint8_t>(lib::RoundEven(value)); }

const UnitPrototypeRecord* FindPrototype(std::string_view name) { return g_unitDatabase.prototypes.FindByName(name); }
const UnitNpcRecord* FindNpc(std::string_view name) { return g_unitDatabase.npcs.FindByName(name); }

// The prototype's model.
const UnitModelRecord* ModelOf(const UnitPrototypeRecord* prototype) {
    return unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
}

// --- The statistics both builders fill the same way ---------------------------------------

// cos(angle / 2) with the angle in degrees.
float HalfAngleCosine(const UnitModelRecord* model) {
    float half = model->angle * 0.5f;
    return lib::Cos(half * ei::kRadiansPerDegree);
}

float RightAngleCosine() { return lib::Cos(ei::kRadiansPerDegree * 90.0f); }

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
    // Truncated as _ftol does (the low half of the 64-bit integer; 0 for a NaN).
    for (int i = 0; i < 3; ++i)
        stats->skills[3 + i] = static_cast<std::uint8_t>(static_cast<std::int64_t>(prototype->skills3to5[i]));
}

// The body slots from the model: a slot with a share is working, with its type and vital mask
// from its level record and no layers; the others unused.
void SetSlots(UnitStats* stats, const UnitModelRecord* model) {
    for (int i = 0; i < 6; ++i) {
        const UnitModelSlot& entry = model->slots[i];
        UnitStatSlot& slot = stats->slots[i];
        float share = entry.share;
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
    for (const auto& name : npc->skillNames) {
        ::SkillLevelRecord* skill = g_skillDatabase.FindLevel(name);
        if (skill) stats->LearnSkill(&g_skillDatabase, skill);
    }
    stats->spentExperience = stats->experience;
}

constexpr float kOneF = 1.0f;
constexpr float kPercent = 0.01f;
constexpr float kTwelve = 12.0f;
constexpr float kPointTwo = 0.2f;
constexpr float kTenF = 10.0f;
constexpr float kMinusTwo = -2.0f;
constexpr int kSkillBodyB0 = 15;
constexpr int kSkillHitPoints = 16;
constexpr int kSkillRate = 20;
constexpr int kSkillCapacity = 21;
constexpr int kSkillStat10 = 23;
constexpr int kSkillStat18 = 24;
constexpr int kSkillStat20 = 25;

// hpVal[3] ^ (ln(experience / hpVal[2]) / ln(hpVal[4])).
float HitPointExperienceCurve(const float* experience) {
    const float exponent =
        lib::Log(*experience / g_logicSettings.hpVal[2]) / lib::Log(g_logicSettings.hpVal[4]);
    return lib::Pow(g_logicSettings.hpVal[3], exponent);
}

float LevelOrZero(UnitStats* stats, int skill) {
    const SkillLevelRecord* record = stats->SkillLevelRecord(skill);
    return record ? record->Value() : 0.0f;
}

// The hit point maximum of the experience curve with skill 16 (the level byte read now).
float CurveHitPoints(UnitStats* stats) {
    float curve = HitPointExperienceCurve(&stats->experience);
    float scaled = curve * (stats->strength / g_logicSettings.hpVal[0] * g_logicSettings.hpVal[1]);
    return (LevelOrZero(stats, kSkillHitPoints) * kPercent + kOneF) * scaled;
}

constexpr float kStatBase = 25.0f;

// The level value of skill `index` stored to a float; 0 without one.
float LevelAsFloat(const UnitStats* stats, int index) {
    std::uint8_t level = stats->skillLevels[index];   // unchecked
    if (level == 0) return 0.0f;
    const SkillLevelRecord* record = unitdb::SkillLevelAt(static_cast<std::uint32_t>(index * 3 - 1 + level));
    if (!record) return 0.0f;
    return record->Value();
}

float WeaponBonus(Unit* unit, int skill) {
    Item* weapon = unit->GetWeapon(unit->activeWeapon);
    if (!weapon) return 0.0f;
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
    MapObjectType type = {};
    EngineObject* (*create)() = nullptr;
};
// The class's entry: its id and a new object of it.
template <class T>
constexpr FactoryEntry Make(MapObjectType type) {
    return {type, [] () -> EngineObject* { return new T(); }};
}
constexpr FactoryEntry kFactory[] = {
    Make<Unit>(MapObjectType::Hero),
    Make<Unit>(MapObjectType::Creature),
    Make<Unit>(MapObjectType::UnitSpare),
    Make<CTree>(MapObjectType::Tree),
    Make<CBuilding>(MapObjectType::Building),
    Make<CStone>(MapObjectType::Stone),
    Make<CStuffObject>(MapObjectType::StuffObject),
    Make<TorchObject>(MapObjectType::Torch),
    Make<MagicTrap>(MapObjectType::MagicTrap),
    Make<Lever>(MapObjectType::Lever),
    Make<FigureAnimationState>(MapObjectType::AnimationState),
    Make<TrapObject>(MapObjectType::Trap),
};

} // namespace

// --- Name lookups -----------------------------------------------------------------------------

UnitNpcRecord* NpcPrototypeTable::FindByName(std::string_view name) {
    auto found = byName.find(std::string(name));
    return found != byName.end() ? found->second : nullptr;
}

// --- Builders ---------------------------------------------------------------------------------

void MapObjectManager::SetDefaults(UnitVitals* vitals, UnitStats* stats) {
    if (!stats) {
        vitals->hitPoints = kDefaultVitalPool;
        vitals->maxHitPoints = kDefaultVitalPool;
        vitals->hitPointRegen = kDefaultRegen;
        vitals->mana = kDefaultVitalPool;
        vitals->maxMana = kDefaultVitalPool;
        vitals->manaRegen = kDefaultRegen;
        for (UnitStatPair& protection : vitals->protection) {
            protection.current = kDefaultFullLevel;
            protection.base = kDefaultFullLevel;
        }
        return;
    }
    stats->experience = 0.0f;
    stats->strengthBase = kDefaultStat;
    stats->dexterityBase = kDefaultStat;
    stats->intelligenceBase = kDefaultStat;
    for (float& modeSpeed : stats->modeSpeeds) modeSpeed = kDefaultModeSpeed;
    stats->spentExperience = 0.0f;
    stats->speedBase = kDefaultSpeed;
    stats->rateBase = kDefaultRateBase;
    stats->capacity = 0.0f;
    stats->movementKind = 0;
    for (int i = 0; i < 6; ++i) {
        stats->perception[i].base = kDefaultFullLevel;
        stats->perception[i].current = kDefaultFullLevel;
        stats->conspicuity[i].base = kDefaultFullLevel;
        stats->conspicuity[i].current = kDefaultFullLevel;
    }
    for (UnitStatSlot& slot : stats->slots) {
        slot.type = 1;
        slot.vitalMask = 0;
        slot.kind = kSlotWorking;
        slot.current = kDefaultFullLevel;
        slot.maximum = kDefaultFullLevel;
        for (UnitStatLayer& layer : slot.layers) {
            layer.layerMask = 0;
            layer.state = 0;
            layer.reserved[0] = 0;
            layer.reserved[1] = 0;
            std::memset(layer.absorb, 0, sizeof(layer.absorb));
        }
    }
}

int MapObjectManager::BuildFromPrototype(UnitVitals* vitals, UnitStats* stats, std::string_view name) {
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) {
        SetDefaults(vitals, stats);
        return 1;
    }
    const UnitModelRecord* model = ModelOf(prototype);
    const UnitNpcRecord* npc = FindNpc(name);
    if (!vitals || !stats) return 0;
    // The vitals start zeroed: the fields their reset and this builder leave are 0, not what the caller's stack
    // held (a determinism fix; the harness zeroes them in the original too). The statistics' reset zeroes all.
    std::memset(vitals, 0, sizeof(UnitVitals));
    vitals->Reset();
    stats->Reset();

    vitals->maxHitPoints = prototype->hitPoints;
    vitals->hitPoints = prototype->hitPoints;
    CopyDword(vitals->hitPointRegen, model->hitPointRegen);
    vitals->maxMana = prototype->mana;
    vitals->mana = prototype->mana;
    CopyDword(vitals->manaRegen, model->manaRegen);
    stats->fixedMaxHitPoints = vitals->maxHitPoints;

    if (npc) {
        stats->strengthBase = npc->stats[0];
        stats->strength = npc->stats[0];
        stats->dexterityBase = npc->stats[1];
        stats->dexterity = npc->stats[1];
        stats->intelligenceBase = npc->stats[2];
        stats->intelligence = npc->stats[2];
        float rate = stats->dexterity * 0.5f;
        CopyDword(stats->speedBase, prototype->speed);
        stats->rateBase = rate;
    } else {
        float strength = prototype->hitPoints * kThird;
        stats->strengthBase = strength;
        stats->strength = strength;
        float dexterity = prototype->mana * kThird;
        stats->dexterityBase = dexterity;
        stats->dexterity = dexterity;
        stats->intelligenceBase = kStat25;
        stats->intelligence = kStat25;
        stats->rateBase = kRate15;
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
        float value = model->protection[i] * prototype->protectionScale;
        vitals->protection[i].base = value;
        vitals->protection[i].current = value;
    }
    SetSlots(stats, model);
    SetCombat(stats, prototype, model);
    LearnNpcSkills(stats, npc);
    return 1;
}

void BuildNpcStats(UnitStats* stats, const std::string& name) {
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    const UnitNpcRecord* npc = FindNpc(name);
    if (!prototype) game::FatalExit("Can't find prototype \"%s\" for new HERO.\n", name);
    const UnitModelRecord* model = ModelOf(prototype);
    if (!npc) game::DebugLog("Can't find NPC prototype \"%s\" for new HERO.\n", name);
    stats->Reset();

    const float* speed = &prototype->speed;
    if (npc) {
        stats->strengthBase = npc->stats[0];
        stats->strength = npc->stats[0];
        stats->dexterityBase = npc->stats[1];
        stats->dexterity = npc->stats[1];
        stats->intelligenceBase = npc->stats[2];
        stats->intelligence = npc->stats[2];
        stats->speedBase = *speed;
        stats->speed = *speed;
        float rate = stats->dexterity * 0.5f;
        stats->rateBase = rate;
        stats->rate = rate;
    } else {
        float strength = prototype->hitPoints * kThird;
        stats->strengthBase = strength;
        stats->strength = strength;
        float dexterity = prototype->mana * kThird;
        stats->dexterityBase = dexterity;
        stats->dexterity = dexterity;
        stats->intelligenceBase = kStat25;
        stats->intelligence = kStat25;
        stats->speedBase = *speed;
        stats->speed = *speed;
        stats->rateBase = kRate15;
        stats->rate = kRate15;
    }
    CopyDword(stats->attackCategory, prototype->attackCategory);
    std::memcpy(stats->modeSpeeds, model->modeSpeeds, sizeof(stats->modeSpeeds));
    for (int i = 0; i < 6; ++i) {
        stats->perception[i].base = prototype->perception[i];
        stats->perception[i].current = prototype->perception[i];
        stats->conspicuity[i].base = prototype->conspicuity[i];
        stats->conspicuity[i].current = prototype->conspicuity[i];
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
}

MapObjectType PrototypeObjectClass(std::string_view name) {
    constexpr MapObjectType kDefaultClass = MapObjectType::Creature;
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) return kDefaultClass;
    const UnitModelRecord* model = ModelOf(prototype);
    if (!model) return kDefaultClass;
    return static_cast<MapObjectType>(model->objectClass);
}

// --- Statistics from their bases -----------------------------------------------------------------

void UnitStats::ResetToBase(float newLoad) {
    strength = strengthBase;
    dexterity = dexterityBase;
    intelligence = intelligenceBase;
    for (int i = 0; i < 6; ++i) {
        perception[i].current = perception[i].base;
        conspicuity[i].current = conspicuity[i].base;
    }
    float bonus = 0.0f;
    if (const ::SkillLevelRecord* record = SkillLevelRecord(kSkillBodyB0)) {
        float level = record->Value();
        if (level != 0.0f) bonus = level * perception[Sense::DaySight].current * kPercent;
    }
    perception[Sense::DaySight].current = bonus + perception[Sense::DaySight].current;
    strength = LevelOrZero(this, kSkillStat10) + strength;
    dexterity = LevelOrZero(this, kSkillStat18) + dexterity;
    sightCone = kMinusTwo;
    intelligence = LevelOrZero(this, kSkillStat20) + intelligence;

    float curve = HitPointExperienceCurve(&experience);
    float hitPoints = curve * (strength / g_logicSettings.hpVal[0] * g_logicSettings.hpVal[1]);
    float perShare = (LevelOrZero(this, kSkillHitPoints) * kPercent + kOneF) * hitPoints / slots[1].share;
    for (UnitStatSlot& slot : slots) {
        if (slot.kind <= 0) continue;
        float maximum = perShare * slot.share;
        slot.maximum = maximum;
        std::uint32_t current = std::bit_cast<std::uint32_t>(slot.current);
        std::uint32_t chosen = maximum >= slot.current ? current : std::bit_cast<std::uint32_t>(maximum);
        slot.current = std::bit_cast<float>(chosen);
    }

    load = newLoad;   // a dword copy
    float strengthCapacity = strength * kTwelve;
    capacity = (LevelOrZero(this, kSkillCapacity) * kPercent + kOneF) * strengthCapacity;
    float legs = LegsFactor();
    loadFactor = legs;
    speed = legs * speedBase;
    armsFactor = ArmsFactor();
    rateBase = dexterity * kPointTwo + kTenF;
    rate = (LevelOrZero(this, kSkillRate) * kPercent + kOneF) * armsFactor * rateBase;
}

void UnitStats::LearnSkill(const SkillDatabase* database, const ::SkillLevelRecord* record) {
    float oldMaximum = CurveHitPoints(this);
    float oldHitPoints = Reduce(oldMaximum);
    int index = const_cast<SkillDatabase*>(database)->IndexOfLevel(record);
    int skill = index / 3;
    int level = index % 3;
    std::uint8_t& known = skillLevels[skill];   // unchecked
    if (known != 0 && level <= known - 1) return;
    known = static_cast<std::uint8_t>(level + 1);
    float newMaximum = CurveHitPoints(this);
    if (oldMaximum <= kOneF) return;
    if (newMaximum <= kOneF) return;
    float newHitPoints = Reduce(newMaximum);
    float heal = oldHitPoints / oldMaximum * newMaximum - newHitPoints;
    HealSlots(newMaximum, heal);
}

float UnitStats::WeaponSkillBonus(WeaponType weaponType, Unit* unit) {
    int type = static_cast<int>(weaponType);
    if (type < 5 || type == 5 || type == 6) {
        int skill = type >= 5 ? 1 : 0;
        float level = LevelAsFloat(this, type);
        if (!IsTextKey(unit->id)) return static_cast<float>(skills[skill]) + level;
        float weapon = WeaponBonus(unit, skill);
        float value = static_cast<float>(static_cast<int>(skills[skill]));
        const SkillRecord* record = unitdb::SkillAt(static_cast<std::uint32_t>(skill));
        char stat = record->statNames.begin()[0][0];   // its first stat's letter
        float base;
        if (stat == 's') base = strength - kStatBase;
        else if (stat == 'i') base = intelligence - kStatBase;
        else if (stat == 'd') base = dexterity - kStatBase;
        else return (value + weapon) + level;
        return ((base + value) + weapon) + level;
    }
    if (type == 8) {
        if (!IsTextKey(unit->id)) return static_cast<float>(skills[0]);
        float weapon = WeaponBonus(unit, 0);
        return SkillWithBonus(0) + weapon;
    }
    return 0.0f;
}

bool UnitStats::CanLearn(const SkillDatabase* database, const ::SkillLevelRecord* record) {
    if (!record->learnable) return false;
    if (strength < record->RequiredStat(0)) return false;
    if (dexterity < record->RequiredStat(1)) return false;
    if (intelligence < record->RequiredStat(2)) return false;
    if (NamesRequirement(record->requiredSkillName)) {
        int skill = record->RequiredSkill();
        if (skill == ::SkillLevelRecord::kNoSkill) return false;
        if (static_cast<std::uint32_t>(skills[skill]) < record->RequiredSkillValue()) return false;   // unchecked
    }
    if (NamesRequirement(record->requiredLevelName)) {
        int required = database->IndexOf(record->requiredLevelName);
        if (skillLevels[required / 3] != static_cast<std::uint32_t>(required % 3 + 1)) return false;
    }
    int group = const_cast<SkillDatabase*>(database)->IndexOfLevel(record) / 3;
    for (int k = 0; k < 26; ++k) {
        std::uint8_t level = skillLevels[k];
        if (level == 0) continue;
        const ::SkillLevelRecord* known = LevelRecordAt(static_cast<std::uint32_t>(3 * k + level - 1));
        if (!known || known->exclusionGroup != record->exclusionGroup) continue;
        if (const_cast<SkillDatabase*>(database)->IndexOfLevel(known) / 3 != group) return false;
    }
    int own = database->IndexOf(record->name);
    return own % 3 > static_cast<int>(skillLevels[own / 3]) - 1;
}

void MapObjectManager::ApplyPrototype(MapObject* object, const std::string& name) {
    UnitVitals vitals{};   // zeroed (the stack's old contents are not kept)
    UnitStats stats{};
    const UnitPrototypeRecord* prototype = FindPrototype(name);
    if (!prototype) {
        vitals.Reset();
        if (object->GetClassId() != ClassId::Unit) {
            SetDefaults(&vitals, nullptr);
            object->SetServerData(&vitals);
        } else {
            stats.Reset();
            SetDefaults(&vitals, &stats);
            object->SetStats(&stats);
        }
        object->ScaleEffectChanged(false);
        object->prototypeId = 0;
        game::DebugLog("Can't find prototype \"%s\" for object %d.\n", name, object->id);
        return;
    }
    const UnitModelRecord* model = ModelOf(prototype);
    if (object->GetClassId() == ClassId::Unit) {
        BuildFromPrototype(&vitals, &stats, name);
        stats.builtFlags |= 1;
        vitals.builtFlags |= 1;
        object->MapObject::SetServerData(&vitals);
        object->SetStats(&stats);
        float lift = -model->height;

        Object3D* figure = object;
        figure->defaultScale.x = 0.0f;
        figure->defaultScale.y = lift;
        figure->defaultScale.z = 0.0f;
        Vector3 scale;
        static_assert(sizeof(scale) == sizeof(prototype->scale));
        scale = prototype->scale;
        object->SetFigureScale(scale);
        object->baseScale = scale;
        object->ApplyBaseScale();
        UnitOrNull(object)->SetSecondPrototype(prototype);
    }
    UnitPrototypeRecord* record = g_unitDatabase.prototypes.FindByName(name);
    object->prototypeId = record ? static_cast<std::uint32_t>(record - g_unitDatabase.prototypes.records.data()) : 0xFFFFFFFFu;
}

// --- The factory ------------------------------------------------------------------------------

EngineObject* MapObjectManager::NewObject(int objectType) {
    for (const FactoryEntry& entry : kFactory) {
        if (entry.type != static_cast<MapObjectType>(objectType)) continue;
        return entry.create();
    }
    return nullptr;
}
