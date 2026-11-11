#pragma once

// The spell database (spells.sdb of the resource archive; spell_db.cpp): the spells, the spell modifiers and the
// trader stock's spell recipes, in the record tables of item_db.h, indexed by code. A lookup by index gives null out
// of range. See docs/notes/items_spells.md ("Databases and naming").

#include "game/items/item_db.h"

#include "lib/vecmath.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class ChunkArchive;

// The spell schools (SpellRecord::school; the trader stock's levels per school), in the order of the cast particle
// effects. None is OrderPlan::castKind of a plan that casts nothing.
inline constexpr int kSchoolCount = 8;
enum class SpellSchool : int {
    Fire = 0,
    Lightning = 1,
    Acid = 2,
    Illusion = 3,
    Divination = 4,
    Enchantment = 5,
    Healing = 6,
    Domination = 7,
    None = 8,
};

namespace itemdb {

// A spell prototype: the fields the camp's spell maker reads.
struct SpellRecord {
    // Fields 0-2 the strings, 3-15 price..needsSight (not school), 16 / 17 the id lists, 18 icon, 19 school,
    // 20 allowedModifiers, 21 skill, 22 classMask, 23-27 the light.
    void Serialize(ChunkArchive* ar);

    std::string name;
    // the spell's code (the trader stock's spell instructions).
    std::string code;
    std::string unreadText;             // archive field 2; nothing reads it
    float price = 0.0f;                 // a loot item's price when it stands for the spell (ItemLoot::GetPrice)
    // The magic (0 elemental, 1 sense, 2 astral; the item info's "school_<n>" line, the inventory's spell tab): sense
    // spells are cast without noise and without slowing the caster, elemental ones are planned as kind 2.
    int castType = 0;
    SpellSchool school = SpellSchool::Fire;
    float mana = 0.0f;
    // fields 6-14: what Spell::InitFromPrototype / SpellContainer::Build copy into the spell, and the item
    // info shows.
    std::uint32_t complexity = 0;       // +1 per modifier in the spell
    float speed = 0.0f;                 // missile speed (the item info's "power")
    float range = 0.0f;
    float area = 0.0f;                  // the spell's radius (sqrt(area / pi) once built with modifiers)
    float damage = 0.0f;                // the effect's power (the item info's level)
    std::uint32_t targetKind = 0;       // 'u' a unit, 't' a point (target flags 0x10000000 / 0x20000000)
    std::uint32_t targetCount = 0;
    std::uint32_t duration = 0;         // ticks
    std::uint32_t castTime = 0;         // Spell::castTime (UnitServer::CastValue: ticks of the cast animation)
    int needsSight = 0;                // needs a line of sight to its target
    int skill = 0;                      // the school skill it needs
    std::vector<int> unreadIdListA;    // archive chunk 16; nothing reads it
    std::vector<int> unreadIdListB;    // archive chunk 17; nothing reads it
    std::uint32_t icon = 0;             // the number of its picture and figure ("spell%04d", "prototype%04d")
    // The light the spell's effects put up: its colour, range and the divisor of its rise speed (the light rises
    // at lightRange / max(lightRiseDivisor, 1) a tick).
    Vector3 lightColor;
    float lightRange = 0.0f;
    float lightRiseDivisor = 0.0f;
    // which modifiers it accepts, one byte per kind by code letter: 'r', 't', 'a', 'e', 'd'; the item info
    // shows the level when the 'e' byte is set.
    std::uint8_t allowedModifiers[5] = {};
    std::uint32_t classMask = 0;        // bit (class - 1): the class templates that may use it
};

// The spell database's three lists of 0x34-byte records (chunks 3-5 of spells.sdb): the spell recipes of
// scrolls, armours and weapons (item_stock.h's SpellRecipe; ItemStock::FillSpellMaps reads them).
struct SpellListRecord {
    // String 1, the lists as chunks 2 / 3 (one string field 1 per entry; loading erases the list and
    // resizes it first), string 4, field 5.
    void Serialize(ChunkArchive* ar);

    std::string spell;                      // the spell's name
    std::vector<std::string> required;     // modifier names
    std::vector<std::string> optional;     // modifier names
    std::string repeated;                   // the modifier repeated while it fits
    std::uint32_t classMask = 0;            // bit per ItemStock::classIndex
};

// A spell modifier prototype.
struct SpellModifierRecord {
    // String 0, fields 1-6 code..skill, string 7, field 8.
    void Serialize(ChunkArchive* ar);

    std::string name;
    // the modifier's code inline; its first one or two letters are the kind the trader stock keys it by (a, d,
    // e, m, r, t; fi, ii).
    char code[4] = {};
    float price = 0.0f;                 // a loot item's price when it stands for the modifier (ItemLoot::GetPrice)
    std::uint32_t kind = 0;
    float mana = 0.0f;                  // added to the spell's
    float strength = 0.0f;              // the trader stock keeps the strongest per kind; kind 'm' scales the cost
    int skill = 0;                      // added to the spell's
    std::string unreadText;             // archive field 7; nothing reads it
    std::uint32_t classMask = 0;        // bit (class - 1): the class templates that may use it
};

// The text a table files a record under: a spell's or a modifier's code (the modifier's four chars read as a C
// string).
inline std::string_view IndexKey(const SpellRecord& record) { return record.code; }
inline std::string_view IndexKey(const SpellModifierRecord& record) { return record.code; }

using SpellTable = RecordTable<SpellRecord>;
using SpellModifierTable = RecordTable<SpellModifierRecord>;

// The spell database: the spells and the modifiers by code, and the three lists of spells.sdb's chunks 3-5.
struct SpellDatabase {
    // "spells.sdb" of the archive `folder` + `archive` (its TOML too, docs/notes/mods.md "Databases as TOML"):
    // Read. True; an archive that cannot be opened, or one without the file, ends the game.
    bool Load(const std::string& folder, const std::string& archive);
    // The original's spells (chunk 1) and modifiers (chunk 2), each read through an archive of its own, then the
    // three lists through a third; the TOML files merged over them; the spells and modifiers filed under their codes.
    void Read(const RecordFile& file);
    // A TOML file's records merged: [[spell]] and [[modifier]] by code, [[scroll_recipe]], [[armor_recipe]] and
    // [[weapon_recipe]] by spell.
    void MergeToml(const toml::Value& root, const std::string& path);
    std::string ToToml() const;

    SpellTable spells;
    SpellModifierTable modifiers;
    // The recipe lists (chunks 3-5): what the trader stock makes scrolls, armour spells and weapon spells from.
    std::vector<SpellListRecord> scrollRecipes;
    std::vector<SpellListRecord> armorRecipes;
    std::vector<SpellListRecord> weaponRecipes;
};

} // namespace itemdb

// The database: built empty by its static initialiser, destroyed by the exit function it registers (spell_db.cpp);
// the resource switch empties it (SwitchResourceDatabases).
extern itemdb::SpellDatabase g_spellDatabase;

namespace itemdb {

// The records of its tables typed (null when the index is out of range).
inline SpellRecord* SpellAt(std::uint32_t index) { return g_spellDatabase.spells.At(index); }
inline SpellModifierRecord* SpellModifierAt(std::uint32_t index) { return g_spellDatabase.modifiers.At(index); }

} // namespace itemdb
