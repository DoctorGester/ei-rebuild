// The weapon skill bonus on a unit's packed parameters. See unit.h (UnitParams) and
// docs/notes/units.md ("Statistics in play").
#include "game/items/item_db.h"

#include "game/app/logic_settings.h"
#include "game/app/options.h"
#include "game/ui/ui.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"

#include <cstdint>

namespace {
constexpr float kStatBase = 25.0f;
constexpr int kWeaponSkill0Bonus = 0;   // WeaponRecord::skillBonus
constexpr int kWeaponSkill1Bonus = 1;

// The level value (unsigned) of the skill level record of skill `type` at the packed level byte, or null
// without one (a negative `type` has none).
const SkillLevelRecord* LevelRecord(const UnitParams* params, int type) {
    if (type < 0) return nullptr;
    std::uint8_t level = params->skillLevels[type];
    if (level == 0) return nullptr;
    return unitdb::SkillLevelAt(static_cast<std::uint32_t>(type * 3 - 1 + level));
}

// The first letter of skill `skill`'s name: which stat counts ('s', 'd', 'i'); 0 without a record.
char StatLetter(int skill) {
    const SkillRecord* record = unitdb::SkillAt(static_cast<std::uint32_t>(skill));
    if (!record || record->statNames.empty()) return 0;
    return record->statNames.front()[0];
}

// The stat that letter names, minus 25, or false for another letter.
bool StatTerm(const UnitParams* params, char letter, float* term) {
    switch (letter) {
    case 's': *term = params->strength - kStatBase; return true;
    case 'i': *term = params->intelligence - kStatBase; return true;
    case 'd': *term = params->dexterity - kStatBase; return true;
    default: return false;
    }
}

float WeaponByte(const itemdb::WeaponRecord* weapon, int skill) {
    return weapon ? static_cast<float>(weapon->skillBonus[skill]) : 0.0f;
}
} // namespace

float UnitParams::WeaponSkillBonus(WeaponType weaponType, const itemdb::WeaponRecord* weapon) {
    int type = static_cast<int>(weaponType);
    float term;
    if (type < 5) {
        // The sums in the original's order.
        const SkillLevelRecord* record = LevelRecord(this, type);
        float level = record ? record->Value() : 0.0f;
        float bonus = WeaponByte(weapon, kWeaponSkill0Bonus);
        float skill = skills[0];
        if (StatTerm(this, StatLetter(0), &term)) return level + ((term + skill) + bonus);
        return level + (skill + bonus);
    }
    if (type == 5 || type == 6) {
        const SkillLevelRecord* record = LevelRecord(this, type);
        float level = record ? record->Value() : 0.0f;
        float bonus = WeaponByte(weapon, kWeaponSkill1Bonus);
        float skill = skills[1];
        if (StatTerm(this, StatLetter(1), &term)) return ((term + skill) + bonus) + level;
        return (skill + bonus) + level;
    }
    if (type == 8) {
        float bonus = WeaponByte(weapon, kWeaponSkill0Bonus);
        float skill = skills[0];
        if (StatTerm(this, StatLetter(0), &term)) return (term + skill) + bonus;
        return skill + bonus;
    }
    return 0.0f;
}

float UnitParams::Period() { return 25.0f / static_cast<float>(intelligence); }

// --- Protection --------------------------------------------------------------------------------------

// UnitParams::ProtectionBlock: the protection figures the unit panel and the script addon show, from the
// packed parameters and packed vitals. See unit.h.

namespace {
constexpr int kDamageTypes = 7;
constexpr int kSlotKindWorn = 1;   // a slot counts when its kind is above this
// The protection factor by difficulty (GameDifficulty, ui.h): the setting "Absorption".
inline const float* DifficultyFactors() { return g_logicSettings.absorption; }
} // namespace

std::array<float, 7> UnitParams::ProtectionBlock(const UnitVitalsPacked* vitals, std::uint8_t keyed) {
    static_assert(kDamageTypes == 7);
    std::array<float, kDamageTypes> out{};
    float scale = 0.0f;
    if (keyed) {
        float total = 0.0f;
        for (const UnitParamsSlot& slot : slots) {
            if (slot.kind <= kSlotKindWorn) continue;
            float weight = unitdb::kSlotWeights[slot.type];
            for (int k = 0; k < kDamageTypes; ++k)
                out[k] = static_cast<std::uint16_t>(slot.protection[k]) * weight + out[k];
            total += weight;
        }
        if (total > 0.0f) scale = 1.0f / total;
    }
    float factor = keyed ? 1.0f : DifficultyFactors()[options::g_options.difficulty];
    for (int k = 0; k < kDamageTypes; ++k)
        out[k] = (scale * out[k] + static_cast<std::uint16_t>(vitals->protection[k])) * factor;
    return out;
}
