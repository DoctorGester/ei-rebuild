// The statistics refreshed from their bases and the skill rules of UnitStats, and the NPC prototype table's name
// lookup. The creature builders are objects/map_object_manager.cpp's. See unit_create.h and
// docs/notes/units.md ("Creation, databases, save").
#include "game/app/logic_settings.h"
#include "engine/data/text_manager.h"
#include "game/items/item.h"
#include "game/units/unit.h"
#include "game/units/unit_create.h"
#include "game/units/unit_db.h"

#include "lib/crt.h"
#include "lib/maths.h"
#include "lib/str.h"

#include <bit>
#include <cstdint>
#include <string>

namespace {

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

} // namespace

// --- Name lookups -----------------------------------------------------------------------------

UnitNpcRecord* NpcPrototypeTable::FindByName(std::string_view name) {
    auto found = byName.find(std::string(name));
    return found != byName.end() ? found->second : nullptr;
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
