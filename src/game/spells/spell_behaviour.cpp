// What the game's tables read about a spell by its behaviour (UseOf, spell_db.h): one row for each of the game's 42,
// the record's own fields for the others; and the behaviours the load gives the records. See
// docs/notes/items_spells.md ("Spell behaviours").
#include "game/spells/spell_db.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace itemdb {

namespace {

constexpr DamageType kNoDamage = DamageType::Other;

// The game's spells still in code by behaviour (the cut possession, link and charm, shapechange and comprehend): the
// AI's use, the damage type, and offensive / turns the victim / fighting. Those on scripts (assets/spells) have an
// empty row: their records' fields (assets/spells/spells.sdb.toml) say it.
constexpr SpellUse kGameSpells[kGameSpellCount] = {
    {},                                                                   // FireArrow: Missile.lua
    {},                                                                   // Lightning: Lightning.lua
    {},                                                                   // AcidRay: Missile.lua
    {},                                                                   // Fireball: Fireball.lua
    {},                                                                   // InvokeLightning: Strike.lua
    {},                                                                   // AcidColumn: Strike.lua
    {},                                                                   // FireWall: LastingArea.lua
    {},                                                                   // LightningWall: LastingArea.lua
    {},                                                                   // AcidFog: LastingArea.lua
    {},                                                                   // ProtectionFromFire: Buff.lua
    {},                                                                   // ProtectionFromLightning: Buff.lua
    {},                                                                   // ProtectionFromAcid: Buff.lua
    {},                                                                   // EagleSight: Buff.lua
    {},                                                                   // Infravision: Buff.lua
    {},                                                                   // DetectLife: Buff.lua
    {},                                                                   // Invisibility: Buff.lua
    {},                                                                   // Silence: Buff.lua
    {},                                                                   // Lichdom: Buff.lua
    {},                                                                   // Fireworks: Fireworks.lua
    {},                                                                   // Stench: Buff.lua
    {},                                                                   // Clairvoyance: Clairvoyance.lua
    {SpellAi::None, kNoDamage, false, false, false},                      // Possession
    {SpellAi::None, kNoDamage, false, false, false},                      // Shapechange
    {},                                                                   // VisionFog: VisionFog.lua
    {},                                                                   // Healing: Healing.lua
    {},                                                                   // Stun: Buff.lua
    {},                                                                   // Antimagic: Buff.lua
    {SpellAi::None, kNoDamage, false, false, false},                      // Link
    {},                                                                   // Teleport: Teleport.lua
    {},                                                                   // Strengthen: Buff.lua
    {},                                                                   // Weaken: Buff.lua
    {},                                                                   // Regeneration: Buff.lua
    {},                                                                   // Feeblemind: Buff.lua
    {},                                                                   // Speed: Buff.lua
    {},                                                                   // Slow: Buff.lua
    {SpellAi::None, kNoDamage, false, false, false},                      // Comprehend
    {SpellAi::None, kNoDamage, true, false, false},                       // Charm
    {},                                                                   // Enlarge: Buff.lua
    {},                                                                   // Shrink: Buff.lua
    {},                                                                   // Campfire: Campfire.lua
    {},                                                                   // RickMagic: Missile.lua
    {},                                                                   // CurseMagic: Lightning.lua
};

} // namespace

SpellUse UseOf(const SpellRecord& record) {
    const auto behaviour = static_cast<std::uint32_t>(record.behaviour);
    const auto either = [&record](std::int8_t field) { return field < 0 ? record.offensive : field != 0; };
    SpellUse use = behaviour < kGameSpellCount
                       ? kGameSpells[behaviour]
                       : SpellUse{record.ai, record.damageType, record.offensive, either(record.turnsVictim),
                                  either(record.fighting)};
    use.effect = record.aiEffect;
    return use;
}

std::vector<std::string> SetBehaviours(std::vector<SpellRecord>& records) {
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (!records[i].script.empty()) records[i].behaviour = SpellType::Scripted;
        else records[i].behaviour = i < kGameSpellCount ? static_cast<SpellType>(i) : SpellType::None;
        records[i].activeEffect =
            records[i].effect == kOwnEffect ? records[i].behaviour : static_cast<SpellType>(records[i].effect);
        if (records[i].effect == kOwnEffect) records[i].effectKind = static_cast<int>(i);
        else if (records[i].effect == static_cast<int>(SpellType::None)) records[i].effectKind = -1;
        else records[i].effectKind = records[i].effect;
    }
    std::vector<std::string> unnamed;
    for (SpellRecord& record : records) {
        record.aiEffect = record.activeEffect;
        if (static_cast<std::uint32_t>(record.behaviour) < kGameSpellCount || record.aiEffectCode.empty()) continue;
        const auto named = std::find_if(records.begin(), records.end(),
                                        [&](const SpellRecord& other) { return other.code == record.aiEffectCode; });
        if (named != records.end()) record.aiEffect = named->activeEffect;
        else unnamed.push_back(record.code);
    }
    return unnamed;
}

} // namespace itemdb
