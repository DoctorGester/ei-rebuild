#pragma once

// The records of the units database (units.udb, loaded at startup into the prototype, NPC, model and skill
// level tables) and how each one is read from / written to the tagged-chunk archive (model.h,
// ChunkArchive). Names provisional; the field ids are the archive's. See docs/notes/units.md ("Database
// records").

#include "ei/x87.h"
#include "game_data.h"
#include "lib/mfc.h"
#include "maths.h"

#include "lib/stlp.h"
#include "lib/static_object.h"
#include "unit_create.h"   // UnitPrototypeTable, NpcPrototypeTable

#include <bit>
#include <cstddef>
#include <cstdint>

class ChunkArchive;

// An STLport vector<CString> (0xC bytes); it is serialised as chunk `id` holding one field 1 per string.
using CStringVector = stlp::vector<mfc::CString>;
static_assert(sizeof(CStringVector) == 0xC);

// unit_db.cpp. A vector<CString> as chunk `id` holding one field 1 per string, the way the NPC and quest
// briefing serialisers inline it: loading empties the vector (the other serialiser erases the range
// instead, with the same result) and resizes it to the count of fields 1; each string is the next
// occurrence of field 1 (the current chunk's occurrence, set before each).
void InlineStrings(ChunkArchive* ar, int id, CStringVector* strings);

// A unit prototype (0x120 bytes). Fields whose meaning is known are named after what the statistics
// builders (unit_create_stats.cpp) make of them.
// One gold roll of a prototype's loot (hoover_loot.cpp): an amount between `minimum` and
// `maximum`, dropped when a percent roll is within `chance`.
struct UnitGoldRoll {
    float chance;                   // +0x00 percent
    std::uint32_t field1;               // +0x04 field 1 (nothing reads it)
    float minimum;                  // +0x08
    float maximum;                  // +0x0C

    // The four dwords as fields 0..3 (the prototype's second roll, chunk 0x1C).
    void Serialize(ChunkArchive* ar);
};
static_assert(sizeof(UnitGoldRoll) == 0x10);

struct UnitPrototypeRecord {
    // Body of a virtual (registered by member pointer).
    UnitPrototypeRecord* DeleteScalar(unsigned flags);
    // Reads (archive loading) or writes the record's fields.
    void Serialize(ChunkArchive* ar);

    mfc::CString name;              // +0x00 field 0
    mfc::CString modelName;         // +0x04 1
    int modelIndex;                 // +0x08 2 (replaced by the model's index at load)
    int skin;                       // +0x0C 3
    int hair;                       // +0x10 4 (-1: none)
    Vector3 scale;                  // +0x14 5, 6, 7
    std::uint32_t panelViewSize;        // +0x20 0x22 the unit panel's model view size
    std::uint32_t height;               // +0x24 0x23 (float bits) its height: the panel view, reach and small-unit checks
    float viewDistance;             // +0x28 0x25 how far before the unit a dialogue's camera may stand
    float viewHeight;               // +0x2C 0x26 the height of its eyes for a dialogue's camera
    float shake;                    // +0x30 0x28 the camera shake of its steps and attacks
    mfc::CString soundFolder;       // +0x34 8 (when set, the action sounds come from here, not the model's)
    std::uint8_t unused_38[0x10];  // not serialised
    float hitPoints;                // +0x48 9
    float mana;                     // +0x4C 10
    std::uint32_t level;            // +0x50 0x29 (CUnit::Level of a unit without a text key)
    std::uint32_t experienceValue;      // +0x54 0x2B the experience its death awards
    float protectionScale;          // +0x58 11
    std::uint32_t field12;              // +0x5C 12 (-> statistics +0x8C; nothing reads it)
    float speed;                    // +0x60 13
    float combat[5];                // +0x64 14, 15, 0x24, 16, 17
    std::uint32_t naturalWeapon;    // +0x78 0x12
    std::uint32_t hitSoundKind;     // +0x7C 0x27 (low byte) the weapon class of its hits: the hit sound class (CUnitClientSpecific::HitEffect)
    float damage;                   // +0x80 0x13
    float damageSpread;             // +0x84 0x14
    float skillLevel;               // +0x88 0x15 every skill and attribute byte starts here (rounded)
    float attributes01;             // +0x8C 0x16 attributes 0 and 1
    float attribute2;               // +0x90 0x17 attribute 2
    std::uint32_t peripheralRange;      // +0x94 0x18 its peripheral sight range (-> statistics +0x48)
    float valuesB0[6];              // +0x98 0x19
    float valuesE0[6];              // +0xB0 0x1A
    UnitGoldRoll gold;              // +0xC8 chunk 0x1B, fields 0..3 (the loot, hoover_loot.cpp)
    UnitGoldRoll secondGold;        // +0xD8 chunk 0x1C (fields 0..3)
    CStringVector lootItems;            // +0xE8 0x1D item names its loot draws from (hoover_loot.cpp)
    float skills3to5[3];            // +0xF4 0x1E
    // The starting equipment a new unit (unit_equipment.cpp): spells for every unit; armour pieces and two
    // weapons (item names, may be empty) for units of type 0x32.
    CStringVector spellNames;       // +0x100 0x1F
    CStringVector armourNames;      // +0x10C 0x20
    mfc::CString weaponName;        // +0x118 0x21 (its slot becomes the unit's +0x270)
    mfc::CString secondWeaponName;  // +0x11C 0x2A
};
static_assert(offsetof(UnitPrototypeRecord, soundFolder) == 0x34);
static_assert(offsetof(UnitPrototypeRecord, hitPoints) == 0x48);
static_assert(offsetof(UnitPrototypeRecord, naturalWeapon) == 0x78);
static_assert(offsetof(UnitPrototypeRecord, valuesB0) == 0x98);
static_assert(offsetof(UnitPrototypeRecord, gold) == 0xC8);
static_assert(offsetof(UnitPrototypeRecord, lootItems) == 0xE8);
static_assert(offsetof(UnitPrototypeRecord, spellNames) == 0x100);
static_assert(offsetof(UnitPrototypeRecord, armourNames) == 0x10C);
static_assert(offsetof(UnitPrototypeRecord, weaponName) == 0x118);
static_assert(sizeof(UnitPrototypeRecord) == 0x120);

// A named creature's statistics (0x60 bytes, found by the prototype name). unit_create_stats.cpp reads the
// experience, stats, attribute and skill bytes and the skill names.
struct UnitNpcRecord {
    // Reads (archive loading) or writes the record's fields.
    void Serialize(ChunkArchive* ar);

    mfc::CString name;              // +0x00 field 0 (the prototype's name)
    std::uint32_t field1;               // +0x04 1 (nothing reads it)
    float experience;               // +0x08 2
    float stats[3];                 // +0x0C 3, 4, 5 (strength, dexterity, intelligence)
    std::uint8_t attributes[3];     // +0x18 6 (three bytes)
    std::uint8_t skills[8];         // +0x1B 7 (eight bytes)
    std::uint8_t unused_23;        // not serialised
    CStringVector skillNames;       // +0x24 8 (the skill level records it learns)
    CStringVector startWeapons;     // +0x30 9: item names a multiplayer hero starts with
    CStringVector startQuickItems;  // +0x3C 10: its quick items
    CStringVector startSpells;      // +0x48 11: its spells
    float experienceFloor;          // +0x54 12: its starting experience, which it keeps when it dies in a network game
    int startMoney;                 // +0x58 13: money its party starts with
    std::uint8_t voiced;                // +0x5C 14 (one byte): the multiplayer hero screen offers it as a voice
    std::uint8_t unused_5D[3];
};
static_assert(offsetof(UnitNpcRecord, attributes) == 0x18);
static_assert(offsetof(UnitNpcRecord, skills) == 0x1B);
static_assert(offsetof(UnitNpcRecord, skillNames) == 0x24);
static_assert(offsetof(UnitNpcRecord, experienceFloor) == 0x54);
static_assert(offsetof(UnitNpcRecord, voiced) == 0x5C);
static_assert(sizeof(UnitNpcRecord) == 0x60);

// One of a model's six body slots (0x10 bytes; serialised as fields 0..3 of chunk 0x0C + slot): the skill
// level record's name, its index (resolved at load), the slot's share and factor.
struct UnitModelSlot {
    // The level name as field 0, then levelIndex, share and factor as 1..3.
    void Serialize(ChunkArchive* ar);

    mfc::CString levelName;         // +0x00
    int levelIndex;                 // +0x04
    float share;                    // +0x08
    float factor;                   // +0x0C
};
static_assert(sizeof(UnitModelSlot) == 0x10);

// A unit model (0x168 bytes).
struct UnitModelRecord {
    // Reads (archive loading) or writes the record's fields.
    void Serialize(ChunkArchive* ar);

    mfc::CString name;              // +0x00 field 0
    int objectClass;                // +0x04 1
    float hitPointRegen;            // +0x08 2
    float manaRegen;                // +0x0C 3
    std::uint32_t field4;               // +0x10 4 (nothing reads it)
    std::uint32_t movementKind;         // +0x14 5 (-> statistics +0x60, UnitStats::movementKind)
    float angle;                    // +0x18 6 (degrees)
    std::uint32_t modeSpeeds[4];    // +0x1C 7
    std::uint32_t actionSpeeds[6];  // +0x2C 0x24 (float bits; copied to CUnit::actionSpeeds)
    std::uint32_t field8;               // +0x44 8 (-> statistics +0x64; nothing reads it)
    std::uint32_t sizeClass;        // +0x48 9
    std::uint32_t sizeClassSmall;   // +0x4C 10
    std::uint32_t damageWeights[7]; // +0x50 11
    float protection[7];            // +0x6C 0x27
    UnitModelSlot slots[6];         // +0x88 chunks 0x0C..0x11
    mfc::CString figureName;            // +0xE8 0x1E the figure and skin file prefix ("%sskin_%.2d.mmp")
    CStringVector field1F;              // +0xEC 0x1F (nothing reads it)
    CStringVector field20;              // +0xF8 0x20 (nothing reads it)
    float height;                   // +0x104 0x21 (the object stands this much lower)
    std::uint32_t bloodKind;        // +0x108 0x28 1..4 blood (particles 0x200F / 0x2031..0x2033 and a decal), 5..8 particles only
    int footprintKind;              // +0x10C 0x2A the footprint texture (-1: none)
    std::uint32_t footPart;         // +0x110 0x2B the foot parts "ll<n>" / "rl<n>" (1..8)
    std::uint32_t castEffect;           // +0x114 0x29 nonzero: its casts show an effect (combat_update.cpp)
    mfc::CString hitMaterial;       // +0x118 0x2C the material its hit sounds use ("stone", "leather", ...)
    std::uint8_t swapFeet;          // +0x11C 0x2D (one byte) the first step is the right foot
    std::uint8_t unused_11D[3];
    std::uint32_t sizeScale;            // +0x120 0x2E (float bits) multiplies the unit's size for its size classes
    mfc::CString soundFolder;       // +0x124 0x22 (the step / attack / idle sounds: "<folder>\idle\<n>.wav" ...)
    std::uint8_t unused_128[0x10]; // not serialised
    CStringVector soundNames;       // +0x138 0x23 (the .wav files of each are counted)
    // +0x144 the number of sound files of each soundNames entry (an STLport vector<int>; counted at
    // startup; not serialised).
    stlp::vector<std::uint32_t> soundCounts;

    const std::uint32_t* SoundCounts() const { return soundCounts.begin(); }
    std::uint32_t idleSoundChance;  // +0x150 0x25 percent
    std::uint32_t attackSoundChance;    // +0x154 0x26 percent
    // The eyes on the skin texture (in 256ths of its width; the blink; read as signed ints): left, distance
    // of their bottom from the bottom edge, width, height.
    std::uint32_t eyeLeft;          // +0x158 0x2F
    std::uint32_t eyeBottom;        // +0x15C 0x30
    std::uint32_t eyeWidth;         // +0x160 0x31
    std::uint32_t eyeHeight;        // +0x164 0x32
};
static_assert(offsetof(UnitModelRecord, damageWeights) == 0x50);
static_assert(offsetof(UnitModelRecord, slots) == 0x88);
static_assert(offsetof(UnitModelRecord, figureName) == 0xE8);
static_assert(offsetof(UnitModelRecord, height) == 0x104);
static_assert(offsetof(UnitModelRecord, soundFolder) == 0x124);
static_assert(offsetof(UnitModelRecord, idleSoundChance) == 0x150);
static_assert(offsetof(UnitModelRecord, soundNames) == 0x138);
static_assert(sizeof(UnitModelRecord) == 0x168);
static_assert(offsetof(UnitModelRecord, soundCounts) == 0x144);

// A unit database level record (0x44 bytes; the fourth table).
struct UnitLevelRecord {
    // Body of a virtual (registered by member pointer).
    UnitLevelRecord* DeleteScalar(unsigned flags);
    // The name as field 0, the seven dwords at +0x08 / +0x24 as fields 1 / 2, +0x40 as 3 and +0x04 as 4.
    void Serialize(ChunkArchive* ar);

    mfc::CString name;              // +0x00 field 0
    int slotType;                   // +0x04 4 the type a model's body slot of this level gets (UnitStatSlot::type)
    std::uint32_t valuesA[7];       // +0x08 1
    std::uint32_t valuesB[7];       // +0x24 2
    std::uint32_t vitalMask;        // +0x40 3 the slot's vital mask (UnitStatSlot::vitalMask)
};
static_assert(offsetof(UnitLevelRecord, valuesB) == 0x24 && sizeof(UnitLevelRecord) == 0x44);

// unit_db_destroy.cpp: the prototype, NPC and model records' destructors.
namespace unitdb {
void __fastcall DestroyPrototypeRecord(UnitPrototypeRecord* record);
void __fastcall DestroyNpcRecord(UnitNpcRecord* record);
void __fastcall DestroyModelRecord(UnitModelRecord* record);
// A model slot's destructor (its level name).
void __fastcall DestroyModelSlot(UnitModelSlot* slot);
// A level record's destructor (its name; a jump to ~CString).
void __fastcall DestroyLevelRecord(UnitLevelRecord* record);
// Stlp::_Destroy over CStrings, first to last.
void __fastcall DestroyStringRange(mfc::CString* first, mfc::CString* last);
} // namespace unitdb

// The model table (0x20 bytes, the database's +0x40): the records (an STLport vector) and a
// hash_map<string, record*> from their names at +0x0C, as the prototype tables (unit_create.h).
struct UnitModelTable {
    using NameIndex = stlp::hash_map<stlp::string, UnitModelRecord*, stlp::StringHash>;

    // unit_db_load.cpp. The record named `name`: operator[] on the name index, so an unknown name is
    // inserted with a null record (and null is returned).
    UnitModelRecord* FindByName(const char* name);
    // Record `index`, or null past the end.
    UnitModelRecord* At(std::uint32_t index);

    UnitModelRecord* begin;
    UnitModelRecord* end;
    UnitModelRecord* endOfStorage;
    NameIndex byName;               // +0x0C
};
static_assert(offsetof(UnitModelTable, byName) == 0x0C && sizeof(UnitModelTable) == 0x20);

// The level record table (the database's +0x60), laid out the same way.
struct UnitLevelTable {
    using NameIndex = stlp::hash_map<stlp::string, UnitLevelRecord*, stlp::StringHash>;

    // As UnitModelTable::FindByName.
    UnitLevelRecord* FindByName(const char* name);

    UnitLevelRecord* begin;
    UnitLevelRecord* end;
    UnitLevelRecord* endOfStorage;
    NameIndex byName;               // +0x0C
};
static_assert(offsetof(UnitLevelTable, byName) == 0x0C && sizeof(UnitLevelTable) == 0x20);

// The unit database: four 0x20-byte tables.
struct UnitDatabase {
    // Lists the "*.wav" files of the sound system `sounds`, lower-cases them, then gives each model's sound
    // names (lower-cased in place) the number of files whose name starts with it (soundCounts, resized to
    // the names).
    void CountSoundFiles(void* sounds);
    // unit_db_load.cpp. Rebirth-patched: the vanilla behaviour. Reads "units.udb" of the archive `folder` +
    // `archive`: the level records (chunk 1), the models (2), the prototypes (3) and the NPCs (4), each
    // from its own pass over the file with every record entered into its table's name index; then every
    // prototype's modelIndex becomes the index of the model named by modelName, and every model slot's
    // levelIndex the index of the level record named by levelName (-1 when there is none; the lookups leave
    // a null entry for an unknown name). Returns 1, or what the fatal error report returns when the archive
    // or the file cannot be opened.
    int Load(const char* folder, const char* archive);
    // Destruction and construction in place (database_reset.cpp), which the resource switch runs. The
    // tables from the last to the first, each the name index (its nodes, then its buckets), then the
    // records (in order) and their storage; then the four from the first: no records, an index of 100
    // buckets (193).
    void Reset();

    UnitPrototypeTable prototypes;  // +0x00
    NpcPrototypeTable npcs;         // +0x20
    UnitModelTable models;          // +0x40
    UnitLevelTable levels;          // +0x60
};
static_assert(offsetof(UnitDatabase, npcs) == 0x20);
static_assert(offsetof(UnitDatabase, models) == 0x40);
static_assert(offsetof(UnitDatabase, levels) == 0x60);
static_assert(sizeof(UnitDatabase) == 0x80);

extern lib::StaticObject<UnitDatabase> g_unitDatabase;   // unit_db_destroy.cpp
namespace unitdb {
// The unit database (the prototype table is its first member).
inline UnitDatabase& Database() { return *g_unitDatabase; }
// operator[] on the prototype and model name indexes (an unknown name gets a null entry): the record, or
// null.
inline UnitPrototypeRecord* PrototypeNamed(const stlp::string& name) { return PrototypeTable().byName.Subscript(name); }
// The byte distance of `record` from the first prototype record: what the originals divide by
// 0x120 themselves (a null record included, which gives a negative distance).
inline int PrototypeByteOffset(const UnitPrototypeRecord* record) {
    return static_cast<int>(reinterpret_cast<const char*>(record) - reinterpret_cast<const char*>(PrototypeTable().begin));
}
inline UnitModelRecord* ModelNamed(const stlp::string& name) { return Database().models.byName.Subscript(name); }
} // namespace unitdb

// Eight damage or protection words (an AI attack option's damage, a combat controller's
// protection): word 0 apart, words 1..7 per damage type.
struct DamageWords {
    std::uint16_t words[8];

    // The sum over words 1..7 of what exceeds the matching protection word (a difference with its top bit
    // set counts 0), as 16 bits; leftovers stay in the upper half of EAX (callers read AX).
    std::uint32_t ExcessOver(const DamageWords* protection) const;
};
static_assert(sizeof(DamageWords) == 0x10);

// The skill (perk) database (loaded from "perks.pdb" at startup, Rebirth-patched): the skills, then the
// skill level records (three per skill: levels 1..3). unit_db.cpp.

// A skill (0x18 bytes).
struct SkillRecord {
    // Fields 0..2 and chunk 10 (the stat names, InlineStrings).
    void Serialize(ChunkArchive* ar);
    // The stat names and their storage, field1, name.
    void Destruct();

    mfc::CString name;              // +0x00 field 0
    mfc::CString field1;                // +0x04 1 (nothing reads it)
    std::uint32_t field2;               // +0x08 2 (nothing reads it)
    // +0x0C chunk 10: the stats it counts; the first one's first letter picks the stat ('s'
    // strength, 'd' dexterity, 'i' intelligence; UnitStats::SkillWithBonus).
    CStringVector statNames;
};
static_assert(offsetof(SkillRecord, statNames) == 0x0C && sizeof(SkillRecord) == 0x18);

// A skill level record (0x44 bytes; the perks the trade panel offers, the level's value at +0x34).
struct SkillLevelRecord {
    // Fields 0, 1, 2, 10, 11, 12..22 and 0x17.
    void Serialize(ChunkArchive* ar);
    // The strings +0x14, +0x0C, +0x04, +0x00.
    void Destruct();

    mfc::CString field0;                // +0x00 field 0 (the trade panel's display name of the perk)
    mfc::CString name;              // +0x04 1 (the lookups compare it, _mbsicmp)
    std::uint32_t field2;               // +0x08 2 (nothing reads it)
    mfc::CString requiredLevelName;     // +0x0C 10 the level record it requires ("none")
    std::uint32_t exclusionGroup;       // +0x10 0x17 levels of one group exclude each other across skills (CanLearn)
    mfc::CString requiredSkillName;     // +0x14 11 names the skill byte it requires ("none")
    // +0x18 12..21: [0] the skill byte it requires (an index of UnitStats::skills, named by +0x14)
    // and [2] its least value, [3..5] the least strength, dexterity and intelligence (floats),
    // [6] (+0x30) its worth exponent, [7] (+0x34) the level's value.
    std::uint32_t values[10];
    std::uint8_t learnable;         // +0x40 22 (one byte; UnitStats::CanLearn)
    std::uint8_t unused_41[3];

    int RequiredSkill() const { return static_cast<int>(values[0]); }
    std::uint32_t RequiredSkillValue() const { return values[2]; }
    // stat 0 strength, 1 dexterity, 2 intelligence.
    float RequiredStat(int stat) const { return std::bit_cast<float>(values[3 + stat]); }

    // The level's value (values[7], unsigned), loaded as FILD of a zero-extended qword (exact).
    double Value() const { return x87::FromUnsigned(values[7]); }
    // The worth exponent the skill prices use (values[6], signed).
    int WorthExponent() const { return static_cast<int>(values[6]); }
};
static_assert(offsetof(SkillLevelRecord, values) == 0x18 && sizeof(SkillLevelRecord) == 0x44);

struct SkillDatabase {
    // The level record named `levelName` (_mbsicmp), or null.
    SkillLevelRecord* FindLevel(const char* levelName);
    // The index of the level record named `levelName` (_mbsicmp), or -1.
    int IndexOf(const char* levelName) const;
    // The index of `record` among the level records, or -1.
    int IndexOfLevel(const SkillLevelRecord* record);
    // unit_stat_small.cpp. Skill / level record `index`, or null past the end.
    SkillRecord* SkillAt(std::uint32_t index);
    SkillLevelRecord* LevelAt(std::uint32_t index);
    // unit_db_load.cpp. Rebirth-patched: the vanilla behaviour. Reads "perks.pdb" of the archive `folder` +
    // `archive`: the skills (chunk 1) and the level records (chunk 2, one chunk 1 each). Returns 1, or what
    // the fatal error report returns when the archive or the file cannot be opened.
    int Load(const char* folder, const char* archive);
    // Destruction and construction in place, which the resource switch runs: the level records destroyed
    // and their storage freed, then the skills (through their deleting destructor with 0); both vectors
    // empty.
    void Reset();
    // Both vectors empty. Returns this.
    SkillDatabase* Construct();

    stlp::vector<SkillRecord> skills;        // +0x00
    stlp::vector<SkillLevelRecord> levels;   // +0x0C
};
static_assert(sizeof(stlp::vector<SkillRecord>) == 0x0C);
// The skill database ( unit_db_destroy.cpp).
extern lib::StaticObject<SkillDatabase> g_skillDatabase;
inline SkillDatabase* SkillDb() { return &*g_skillDatabase; }
namespace unitdb {
// Skill level record `index`, or null past the end (what SkillDatabase::LevelAt answers, inline).
inline SkillLevelRecord* SkillLevelAt(std::uint32_t index) {
    stlp::vector<SkillLevelRecord>& levels = SkillDb()->levels;
    return index < static_cast<std::uint32_t>(levels.end() - levels.begin()) ? levels.begin() + index : nullptr;
}
// Skill record `index`, or null past the end (SkillDatabase::SkillAt, inline).
inline SkillRecord* SkillAt(std::uint32_t index) {
    stlp::vector<SkillRecord>& skills = SkillDb()->skills;
    return index < static_cast<std::uint32_t>(skills.end() - skills.begin()) ? skills.begin() + index : nullptr;
}
// The models' level record `index`, or null past the end.
inline UnitLevelRecord* ModelLevelAt(std::uint32_t index) {
    UnitLevelTable& levels = Database().levels;
    return index < static_cast<std::uint32_t>(levels.end - levels.begin) ? levels.begin + index : nullptr;
}
} // namespace unitdb
