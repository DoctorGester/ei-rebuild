#pragma once

// The names the record TOML files give codes, masks and array elements that more than one database uses
// (docs/notes/mods.md, "Readable values"); each database's own are beside its field lists.

#include "engine/data/database_toml.h"

#include <string_view>

namespace recordnames {

// WeaponType: a weapon's type, a unit's natural weapon.
inline constexpr dbtoml::Name kWeaponTypes[] = {
    {0, "sword"}, {1, "axe"},      {2, "dagger"}, {3, "spear"},        {4, "club"},
    {5, "bow"},   {6, "crossbow"}, {8, "none"},   {16, "spells_only"},
};

// The trader class templates of a class mask (bit class - 1: ClassTemplate::type, ItemStock::classIndex), named by
// the vanilla scripts that open each one's camp ("b.smith.constr_1" in basecam, "b.witch.constr_2" in bz2g,
// "b.Shopper.constr_3" in bz8k, "b.Kuzn.constr_4" in bz14h, "b.Rick.constr_5" in bz7g).
inline constexpr dbtoml::Name kTraders[] = {
    {0x1, "gipat_smith"}, {0x2, "gipat_witch"}, {0x4, "ingos_trader"}, {0x8, "suslanger_smith"}, {0x10, "rick"},
};
// A class mask as the traders by name; a class no vanilla script opens (a mod's) by its number, 6..32.
inline dbtoml::Bits SoldBy(std::uint32_t& classMask) {
    return {classMask, kTraders, 0xFFFFFFFFu, 1, "a trader class number 1..32"};
}

// The damage types (DamageType), as the texts "string dmg_0" .. "dmg_6" name them.
inline constexpr std::string_view kDamageTypes[] = {"piercing", "slashing", "crushing", "fire",
                                                    "acid",     "lightning", "general"};

// The spell schools (SpellSchool), as spells.sdb.toml names them and a scripted spell's cast value gives them.
inline constexpr std::string_view kSpellSchools[] = {"fire",        "lightning", "acid",       "illusion", "divination",
                                                     "enchantment", "healing",   "domination", "none"};

// The ground's surface classes (a map tile's class: the footstep sounds, prints.db's tables), by prints.db's names.
// Class 8, which prints.db leaves unnamed, is what Terrain::SurfaceClass answers outside the map and on a tile without
// a texture (kOutsideClass); aiinfo's tileDesc.reg names it "ASTRAL".
inline constexpr std::string_view kSurfaces[] = {
    "grass", "ground", "stone",     "sand",      "rock", "field", "water", "road",
    "astral", "snow", "ice", "dry_grass", "snowballs", "lava", "swamp", "high_rock",
};

// UnitStats::modeSpeeds by movement mode (ModeSpeedOf): a model's and a map unit's.
inline constexpr std::string_view kModeSpeeds[] = {"run", "walk", "crawl", "lie"};
// Sense (the perception and conspicuity rows of a prototype and of a map unit); the sixth is read by nothing (Sense).
inline constexpr std::string_view kSenses[] = {"sight", "day_sight", "near", "hearing", "smell", "unread"};

// UnitStats::attributes: [0] the science of the Use and Steal skills (UnitServer::UseSkill; the trade panel's science
// row, the HUD), [2] the taming (UnitServer::TryTame, kStatTaming); [1] is read by nothing (AttributeWithBonus and
// AttributeWithItems take any index, every caller gives 0 or 2). A quick item's attribute bonuses are by the same index.
inline constexpr std::string_view kAttributes[] = {"science", "unread", "taming"};
// UnitStats::skills, by the skill records' names (UnitStats::SkillWithBonus: skill byte n is perks.pdb's skill n, the
// trade panel's rows, the script's GetSkill): the weapon skills (WeaponSkill: melee 0, archery 1), backstab, the
// magic skills of the schools (SpellSchoolSkill: elemental 3, sense 4, astral 5). Bytes 6 and 7 are read by nothing
// but a perk that requires one (no file has one).
inline constexpr std::string_view kSkillBytes[] = {"melee",  "archery", "backstab", "elemental",
                                                   "sense",  "astral",  "unread_6", "unread_7"};
// A skill byte by its name, for a perk's requirement; "none" the data's 0x7FFFFFFF (SkillLevelRecord::kNoSkill).
inline constexpr dbtoml::Name kRequiredSkills[] = {
    {0, "melee"}, {1, "archery"}, {2, "backstab"}, {3, "elemental"}, {4, "sense"},
    {5, "astral"}, {6, "unread_6"}, {7, "unread_7"}, {0x7FFFFFFF, "none"},
};
// The magic of a spell (SpellRecord::castType: the item info's and the tool tip's "string school_<n>" line, the
// inventory strip's spell tab; sense spells are cast without noise and without the casting's visibility, elemental
// ones are planned as an attack: UnitServer's FinishCast, the move factors, the route planning) and the magic skills a prototype gives (skill bytes 3..5), by the skills' names.
inline constexpr dbtoml::Name kMagic[] = {{0, "elemental"}, {1, "sense"}, {2, "astral"}};
inline constexpr std::string_view kMagicSkills[] = {"elemental", "sense", "astral"};

// MapObjectType: a model's and a map object's class (52: the animals, the code's UnitSpare).
inline constexpr dbtoml::Name kObjectClasses[] = {
    {0x32, "hero"},  {0x33, "creature"}, {0x34, "animal"}, {0x35, "tree"},       {0x36, "building"},
    {0x37, "stone"}, {0x38, "stuff"},    {0x3A, "torch"},  {0x3B, "magic_trap"}, {0x3C, "lever"},
};

} // namespace recordnames
