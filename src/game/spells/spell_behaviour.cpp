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

// The game's spells still in code by behaviour: the AI's use, the damage type, and offensive / turns the victim /
// fighting. Those on scripts (assets/spells) have an empty row: their records' fields (assets/spells/spells.sdb.toml)
// say it. Antimagic is not fighting (Rebirth's FixPlaingBattleMusicFromAntimagic; the original counted it).
constexpr SpellUse kGameSpells[kGameSpellCount] = {
    {},                                                                   // FireArrow: Missile.lua
    {},                                                                   // Lightning: Lightning.lua
    {},                                                                   // AcidRay: Missile.lua
    {SpellAi::AreaAttack, DamageType::Fire, true, false, true},           // Fireball
    {SpellAi::AreaAttack, DamageType::Lightning, true, false, true},      // InvokeLightning
    {SpellAi::AreaAttack, DamageType::Acid, true, false, true},           // AcidColumn
    {SpellAi::LastingArea, DamageType::Fire, true, false, true},          // FireWall
    {SpellAi::LastingArea, DamageType::Lightning, true, false, true},     // LightningWall
    {SpellAi::LastingArea, DamageType::Acid, true, false, true},          // AcidFog
    {SpellAi::Protect, kNoDamage, false, false, false},                   // ProtectionFromFire
    {SpellAi::Protect, kNoDamage, false, false, false},                   // ProtectionFromLightning
    {SpellAi::Protect, kNoDamage, false, false, false},                   // ProtectionFromAcid
    {SpellAi::Enhance, kNoDamage, false, false, false},                   // EagleSight
    {SpellAi::Enhance, kNoDamage, false, false, false},                   // Infravision
    {SpellAi::Enhance, kNoDamage, false, false, false},                   // DetectLife
    {SpellAi::Enhance, kNoDamage, false, false, false},                   // Invisibility
    {SpellAi::Enhance, kNoDamage, false, false, false},                   // Silence
    {SpellAi::Enhance, kNoDamage, true, false, false},                    // Lichdom
    {SpellAi::None, kNoDamage, false, false, false},                      // Fireworks
    {SpellAi::None, kNoDamage, true, true, false},                        // Stench
    {SpellAi::None, kNoDamage, false, false, false},                      // Clairvoyance
    {SpellAi::None, kNoDamage, false, false, false},                      // Possession
    {SpellAi::None, kNoDamage, false, false, false},                      // Shapechange
    {SpellAi::None, kNoDamage, false, false, false},                      // VisionFog
    {SpellAi::Heal, kNoDamage, false, false, false},                      // Healing
    {SpellAi::Curse, kNoDamage, true, true, false},                       // Stun
    {SpellAi::Protect, kNoDamage, false, false, false},                   // Antimagic
    {SpellAi::None, kNoDamage, false, false, false},                      // Link
    {SpellAi::None, kNoDamage, false, false, false},                      // Teleport
    {SpellAi::Protect, kNoDamage, false, false, false},                   // Strengthen
    {SpellAi::Curse, kNoDamage, true, true, false},                       // Weaken
    {SpellAi::Enhance, kNoDamage, false, false, false},                   // Regeneration
    {SpellAi::Curse, kNoDamage, true, true, false},                       // Feeblemind
    {SpellAi::Enhance, kNoDamage, false, false, false},                   // Speed
    {SpellAi::None, kNoDamage, true, true, false},                        // Slow
    {SpellAi::None, kNoDamage, false, false, false},                      // Comprehend
    {SpellAi::None, kNoDamage, true, false, false},                       // Charm
    {SpellAi::None, kNoDamage, false, false, false},                      // Enlarge
    {SpellAi::None, kNoDamage, true, true, false},                        // Shrink
    {SpellAi::None, kNoDamage, false, false, false},                      // Campfire
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
    }
    std::vector<std::string> unnamed;
    for (SpellRecord& record : records) {
        record.aiEffect = record.behaviour;
        if (static_cast<std::uint32_t>(record.behaviour) < kGameSpellCount || record.aiEffectCode.empty()) continue;
        const auto named = std::find_if(records.begin(), records.end(),
                                        [&](const SpellRecord& other) { return other.code == record.aiEffectCode; });
        if (named != records.end()) record.aiEffect = named->behaviour;
        else unnamed.push_back(record.code);
    }
    return unnamed;
}

} // namespace itemdb
