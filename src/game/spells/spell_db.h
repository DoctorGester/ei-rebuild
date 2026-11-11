#pragma once

// The spell database (spells.sdb of the resource archive; spell_db.cpp): the spells, the spell modifiers and the
// trader stock's spell recipes, in the record tables of item_db.h, indexed by code. A lookup by index gives null out
// of range. See docs/notes/items_spells.md ("Databases and naming").

#include "game/items/item_db.h"

#include "lib/vecmath.h"

#include <cstddef>
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

// What a spell does (SpellRecord::behaviour, Spell::Behaviour()): the game's 42 spells, then Scripted and None. The
// game's records have the behaviour of their own place in the spell database (the order of database.res, Spell's
// prototypeId >> 16), so for them the place and the behaviour are one number; a record with a script runs it
// (Scripted), any other record after them does nothing (None).
// Names from the prototypes' names (the code name where it differs), the sound folders under magic\ and the Start*
// functions. The 1.06 database and Rebirth's both have all 42. Shapechange and Comprehend start nothing; Teleport's
// record on the caster stuns it like Stun's (the client draws no particles for it). An active effect holds the place
// of the spell that made it (MapEffect::type); what it does is that record's activeEffect (MapEffect::Behaviour()):
// one of these, the kind the record's `effect` names, else its behaviour.
enum class SpellType : int {
    FireArrow = 0x00,               // "arrow"
    Lightning = 0x01,               // "Lightning Arc"
    AcidRay = 0x02,
    Fireball = 0x03,
    InvokeLightning = 0x04,         // "inv_lit": the light strike
    AcidColumn = 0x05,
    FireWall = 0x06,
    LightningWall = 0x07,           // "litnwall" ("Smtg Lightning"): the light wall
    AcidFog = 0x08,                 // the poison cloud
    ProtectionFromFire = 0x09,      // "prot_fire", sounds in magic\Fireshield
    ProtectionFromLightning = 0x0A, // "prot_electro", magic\Lightsheild
    ProtectionFromAcid = 0x0B,      // "prot_acid", magic\PoisonShield
    EagleSight = 0x0C,
    Infravision = 0x0D,
    DetectLife = 0x0E,
    Invisibility = 0x0F,
    Silence = 0x10,
    Lichdom = 0x11,
    Fireworks = 0x12,
    Stench = 0x13,
    Clairvoyance = 0x14,            // "clairvoyence"
    Possession = 0x15,
    Shapechange = 0x16,
    VisionFog = 0x17,
    Healing = 0x18,
    Stun = 0x19,
    Antimagic = 0x1A,
    Link = 0x1B,
    Teleport = 0x1C,
    Strengthen = 0x1D,              // "strength"
    Weaken = 0x1E,                  // "weak"
    Regeneration = 0x1F,
    Feeblemind = 0x20,
    Speed = 0x21,
    Slow = 0x22,
    Comprehend = 0x23,
    Charm = 0x24,
    Enlarge = 0x25,
    Shrink = 0x26,
    Campfire = 0x27,
    RickMagic = 0x28,               // "rick_magic", school fire: a fire arrow with the RikArrowSmoke trail
    CurseMagic = 0x29,              // "curse_magic", school lightning: lightning (CreateLightning's flag 1)
    Scripted = 0xFE,                // its record's script runs every cast (SpellRecord::script); the tables read its fields
    None = 0xFF,                    // no behaviour of its own: it starts nothing, the tables read the record's fields
};
// The behaviours of the game's own records, places 0 to 0x29.
inline constexpr std::uint32_t kGameSpellCount = 42;

// How the AI uses a spell (SpellRecord::ai; the AI's option kinds, unit_ai.h's AIOptionKind): None it never casts.
enum class SpellAi : int {
    None = 0,
    Attack = 1,         // at an enemy
    AreaAttack = 2,     // at enemies around a point
    LastingArea = 3,    // an area that hurts for a while
    Curse = 4,          // an effect on an enemy that does not have it
    Protect = 5,        // an effect on an ally that does not have it
    Enhance = 6,        // the same, not when only allies are chosen
    Heal = 7,
};

// A spell's place is a byte in the values a spell copies (SpellValues::prototypeIndex) and in an active effect
// (MapEffect::type, saved and sent to the clients); a modifier's place + 1 a signed byte in a spell's modifier list
// (Spell::modifiers). The load leaves out the records past these (SpellDatabase::Read).
inline constexpr std::size_t kMaxSpellRecords = 256;
inline constexpr std::size_t kMaxModifierRecords = 127;

// A record's `effect` not given: its active effects do what its behaviour does (SpellRecord::effect).
inline constexpr int kOwnEffect = -1;

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
    // TOML only (`script`): the Lua file below Res\spells\ (through the mod layers) that runs the spell's casts
    // (docs/notes/items_spells.md, "Scripted spells").
    std::string script;
    // What the spell does, set by the load (SetBehaviours; not in spells.sdb or its TOML).
    SpellType behaviour = SpellType::None;
    // TOML only (`ai`, `damage_type`, `offensive`, `turns_victim`, `fighting`): what the game's tables read for a
    // record whose behaviour has no row of its own (UseOf); turnsVictim and fighting -1: as `offensive`.
    SpellAi ai = SpellAi::None;
    DamageType damageType = DamageType::Other;
    bool offensive = false;
    std::int8_t turnsVictim = -1;
    std::int8_t fighting = -1;
    // TOML only (`cast_school`): the school whose casting look the caster shows (FillCastKind), when it is not the
    // record's own; None: its own.
    SpellSchool castSchool = SpellSchool::None;
    // TOML only (`per_target`): CastSpell casts it once at each target it picks (the requested one, then the units
    // the caster may aim at, up to its target count), as the game's attack spells; without it a scripted spell is one
    // cast whose script picks its targets.
    bool perTarget = false;
    // TOML only (`ends_with_target`): a cast ends at once, with no hook, when its target leaves the world (as every
    // spell of the game's does); without it a scripted cast goes on without its target.
    bool endsWithTarget = false;
    // TOML only (`max_ticks`): the ticks a scripted cast lives at most; 0: no limit (its script ends it, as the walls'
    // outlast their duration); -1, not given: the duration rule (ScriptSpellHost's Settle).
    int maxTicks = -1;
    // TOML only (`item_heals`): an item's spell of the record heals: armour casts it when its wearer is not hit (the
    // others when it is), and an item's periodic cast of it waits until the wearer is hurt, one such cast a tick
    // (UnitServer::TriggerItemSpells, TickItemSpells).
    bool itemHeals = false;
    // TOML only (`ai_effect`): the code of the spell whose effect the AI's curse, protect and enhance options look
    // for on a unit (a scripted spell that applies another's buff names that one); empty: the record's own.
    std::string aiEffectCode;
    // The behaviour those options look for (HasEffect), set by the load (SetBehaviours): the activeEffect of the
    // record aiEffectCode names, else its own.
    SpellType aiEffect = SpellType::None;
    // TOML only (`effect`, by the names of kEffectKinds): what an active effect of this record does, its stat effect
    // and its look on the unit (a SpellType); kOwnEffect: its behaviour's.
    int effect = kOwnEffect;
    // That, set by the load (SetBehaviours): what MapEffect::Behaviour() gives for an effect of this record.
    SpellType activeEffect = SpellType::None;
};

// How the game's tables use a spell (UseOf).
struct SpellUse {
    SpellAi ai = SpellAi::None;
    DamageType damageType = DamageType::Other;  // the AI's estimate of its damage (UnitServer::GetSpellDamage)
    bool offensive = false;     // the HUD's use at once aims it at the nearest enemy, not at the caster
    bool turnsVictim = false;   // the unit it is cast at sees the caster and counts as attacked (CreateMissile)
    bool fighting = false;      // casting it is fighting (UnitServer::IsFighting: the battle music)
    SpellType effect = SpellType::None;  // what the AI's curse / protect / enhance options look for (aiEffect)
};

// The row of the record's behaviour for the game's 42 still in code; for the others (Scripted, None) the record's
// fields, `offensive` also standing for turnsVictim and fighting when they are not given. `effect` is the record's
// aiEffect.
SpellUse UseOf(const SpellRecord& record);

// Each record's behaviour: Scripted with a script, else from its place (the first kGameSpellCount their own, the
// others None); its activeEffect: the kind its `effect` names, else its behaviour; then each one's aiEffect: a game
// row's own behaviour, else the activeEffect of the record its aiEffectCode names (its own when empty or unknown).
// Returns the codes of the records whose aiEffectCode names no record.
std::vector<std::string> SetBehaviours(std::vector<SpellRecord>& records);

// The records past `limit` erased; how many there were.
template <class Record>
std::size_t LeaveOutPast(std::vector<Record>& records, std::size_t limit) {
    if (records.size() <= limit) return 0;
    const std::size_t extra = records.size() - limit;
    records.erase(records.begin() + static_cast<std::ptrdiff_t>(limit), records.end());
    return extra;
}

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

// A file of the game's spells on scripts (assets/spells, built in: the .lua files below Res\spells\'s layers and
// spells.sdb.toml) by name, any case; empty when there is none.
std::string_view BuiltinSpellFile(std::string_view name);

using SpellTable = RecordTable<SpellRecord>;
using SpellModifierTable = RecordTable<SpellModifierRecord>;

// The spell database: the spells and the modifiers by code, and the three lists of spells.sdb's chunks 3-5.
struct SpellDatabase {
    // "spells.sdb" of the archive `folder` + `archive` (its TOML too, docs/notes/mods.md "Databases as TOML"):
    // Read. True; an archive that cannot be opened, or one without the file, ends the game.
    bool Load(const std::string& folder, const std::string& archive);
    // The original's spells (chunk 1) and modifiers (chunk 2), each read through an archive of its own, then the
    // three lists through a third, then the game's own fields (MergeBuiltin); the TOML files merged over them; the
    // spells and modifiers filed under their codes.
    void Read(const RecordFile& file);
    // The fields the build gives the game's spells (assets/spells/spells.sdb.toml: those on scripts) merged into the
    // records of their codes, below every TOML layer; a code the database lacks adds nothing.
    void MergeBuiltin();
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
// The behaviour of the record at `index` (None out of range).
inline SpellType BehaviourAt(std::uint32_t index) {
    const SpellRecord* record = SpellAt(index);
    return record ? record->behaviour : SpellType::None;
}
// What an active effect of the record at `index` does (its activeEffect; None out of range: an effect of a database
// without it).
inline SpellType ActiveEffectAt(std::uint32_t index) {
    const SpellRecord* record = SpellAt(index);
    return record ? record->activeEffect : SpellType::None;
}

} // namespace itemdb
