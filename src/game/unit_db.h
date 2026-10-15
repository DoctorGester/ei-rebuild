#pragma once

// The records of the units database (units.udb, loaded at startup by 0053A200 into the tables at
// 007AF358: prototypes, 007AF378 NPCs, 007AF398 models, 007AF3B8 skill levels) and how each one
// is read from / written to the tagged-chunk archive (model.h, ChunkArchive). Names provisional;
// the field ids are the archive's. See docs/notes/units.md ("Database records").

#include "ei/x87.h"
#include "game_data.h"
#include "lib/mfc.h"
#include "maths.h"

#include "lib/stlp.h"
#include "unit_create.h"   // UnitPrototypeTable, NpcPrototypeTable

#include <bit>
#include <cstddef>
#include <cstdint>

class ChunkArchive;

// An STLport vector<CString> (0xC bytes); 0052DF40 serialises it as chunk `id` holding one field
// 1 per string.
using CStringVector = stlp::vector<mfc::CString>;
static_assert(sizeof(CStringVector) == 0xC);

// 0052D6A0 (thiscall on the values, RET 4; unit_db.cpp, A-U11). Four dwords as fields 0..3.
void __fastcall SerializeFourDwords(std::uint32_t* values, void*, ChunkArchive* ar);

// unit_db.cpp. A vector<CString> as chunk `id` holding one field 1 per string, the way the NPC
// and quest briefing serialisers inline it: loading empties the vector (0052E010; 0052DF40
// erases the range through 0051F970 instead, with the same result) and resizes it to the count
// of fields 1; each string is the next occurrence of field 1 (the current chunk's occurrence, set
// before each).
void InlineStrings(ChunkArchive* ar, int id, CStringVector* strings);

// A unit prototype (0x120 bytes, table 007AF358). Fields whose meaning is known are named after
// what the statistics builders (unit_create_stats.cpp) make of them.
// One gold roll of a prototype's loot (hoover_loot.cpp): an amount between `minimum` and
// `maximum`, dropped when a percent roll is within `chance`.
struct UnitGoldRoll {
    float chance;                   // +0x00 percent
    std::uint32_t field1;               // +0x04 field 1 (nothing reads it)
    float minimum;                  // +0x08
    float maximum;                  // +0x0C
};
static_assert(sizeof(UnitGoldRoll) == 0x10);

struct UnitPrototypeRecord {
    // 0052D740 (RET 4). Reads (archive loading) or writes the record's fields.
    void Serialize(ChunkArchive* ar);

    mfc::CString name;              // +0x00 field 0
    mfc::CString modelName;         // +0x04 1
    int modelIndex;                 // +0x08 2 (0053A200 replaces it by the model's index)
    int skin;                       // +0x0C 3
    int hair;                       // +0x10 4 (-1: none)
    Vector3 scale;                  // +0x14 5, 6, 7
    std::uint32_t panelViewSize;        // +0x20 0x22 the unit panel's model view size
    std::uint32_t height;               // +0x24 0x23 (float bits) its height: the panel view, reach and small-unit checks
    float viewDistance;             // +0x28 0x25 how far before the unit a dialogue's camera may stand (00607BB0)
    float viewHeight;               // +0x2C 0x26 the height of its eyes for a dialogue's camera (00607BB0)
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
    UnitGoldRoll secondGold;        // +0xD8 chunk 0x1C (0052D6A0: fields 0..3)
    CStringVector lootItems;            // +0xE8 0x1D item names its loot draws from (hoover_loot.cpp)
    float skills3to5[3];            // +0xF4 0x1E
    // The starting equipment 005141F0 gives a new unit (unit_equipment.cpp): spells for every
    // unit; armour pieces and two weapons (item names, may be empty) for units of type 0x32.
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

// A named creature's statistics (0x60 bytes, the NPC table 007AF378, found by the prototype name
// through 005145D0). unit_create_stats.cpp reads the experience, stats, attribute and skill bytes
// and the skill names.
struct UnitNpcRecord {
    // 0052DA60 (RET 4). Reads (archive loading) or writes the record's fields.
    void Serialize(ChunkArchive* ar);

    mfc::CString name;              // +0x00 field 0 (the prototype's name)
    std::uint32_t field1;               // +0x04 1 (nothing reads it)
    float experience;               // +0x08 2
    float stats[3];                 // +0x0C 3, 4, 5 (strength, dexterity, intelligence)
    std::uint8_t attributes[3];     // +0x18 6 (three bytes)
    std::uint8_t skills[8];         // +0x1B 7 (eight bytes)
    std::uint8_t unused_23;        // not serialised
    CStringVector skillNames;       // +0x24 8 (the skill level records it learns)
    CStringVector startWeapons;     // +0x30 9: item names a multiplayer hero starts with (006625B0)
    CStringVector startQuickItems;  // +0x3C 10: its quick items
    CStringVector startSpells;      // +0x48 11: its spells
    float experienceFloor;          // +0x54 12: its starting experience (006625B0), which it keeps when it dies in a network game (0066F4C0)
    int startMoney;                 // +0x58 13: money its party starts with (006625B0)
    std::uint8_t voiced;                // +0x5C 14 (one byte): the multiplayer hero screen offers it as a voice
    std::uint8_t unused_5D[3];
};
static_assert(offsetof(UnitNpcRecord, attributes) == 0x18);
static_assert(offsetof(UnitNpcRecord, skills) == 0x1B);
static_assert(offsetof(UnitNpcRecord, skillNames) == 0x24);
static_assert(offsetof(UnitNpcRecord, experienceFloor) == 0x54);
static_assert(offsetof(UnitNpcRecord, voiced) == 0x5C);
static_assert(sizeof(UnitNpcRecord) == 0x60);

// One of a model's six body slots (0x10 bytes; 0052D6F0 serialises it as fields 0..3 of chunk
// 0x0C + slot): the skill level record's name, its index (0053A200 resolves the name), the slot's
// share and factor.
struct UnitModelSlot {
    // 0052D6F0 (RET 4; A-U11). The level name as field 0, then levelIndex, share and factor as 1..3.
    void Serialize(ChunkArchive* ar);

    mfc::CString levelName;         // +0x00
    int levelIndex;                 // +0x04
    float share;                    // +0x08
    float factor;                   // +0x0C
};
static_assert(sizeof(UnitModelSlot) == 0x10);

// A unit model (0x168 bytes, table 007AF398).
struct UnitModelRecord {
    // 0052DC70 (RET 4). Reads (archive loading) or writes the record's fields.
    void Serialize(ChunkArchive* ar);

    mfc::CString name;              // +0x00 field 0
    int objectClass;                // +0x04 1 (00513240)
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
    int footprintKind;              // +0x10C 0x2A the footprint texture (-1: none; 0056D150)
    std::uint32_t footPart;         // +0x110 0x2B the foot parts "ll<n>" / "rl<n>" (1..8)
    std::uint32_t castEffect;           // +0x114 0x29 nonzero: its casts show an effect (combat_update.cpp)
    mfc::CString hitMaterial;       // +0x118 0x2C the material its hit sounds use ("stone", "leather", ...)
    std::uint8_t swapFeet;          // +0x11C 0x2D (one byte) the first step is the right foot
    std::uint8_t unused_11D[3];
    std::uint32_t sizeScale;            // +0x120 0x2E (float bits) multiplies the unit's size for its size classes
    mfc::CString soundFolder;       // +0x124 0x22 (the step / attack / idle sounds: "<folder>\idle\<n>.wav" ...)
    std::uint8_t unused_128[0x10]; // not serialised
    CStringVector soundNames;       // +0x138 0x23 (0053A8E0 counts the .wav files of each)
    // +0x144 the number of sound files of each soundNames entry (an STLport vector<int>; counted
    // at startup, 0053A8E0; not serialised).
    std::uint32_t* soundCounts;
    std::uint32_t* soundCountsEnd;
    std::uint32_t* soundCountsEndOfStorage;

    const std::uint32_t* SoundCounts() const { return soundCounts; }
    std::uint32_t idleSoundChance;  // +0x150 0x25 percent
    std::uint32_t attackSoundChance;    // +0x154 0x26 percent
    // The eyes on the skin texture (in 256ths of its width; the blink, 00538ED0 / 00539010; read
    // as signed ints): left, distance of their bottom from the bottom edge, width, height.
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

// A unit database level record (0x44 bytes; the fourth table, 0053E1B0).
struct UnitLevelRecord {
    // 0052DEE0 (RET 4; A-U11). The name as field 0, the seven dwords at +0x08 / +0x24 as fields
    // 1 / 2, +0x40 as 3 and +0x04 as 4.
    void Serialize(ChunkArchive* ar);

    mfc::CString name;              // +0x00 field 0
    int slotType;                   // +0x04 4 the type a model's body slot of this level gets (UnitStatSlot::type)
    std::uint32_t valuesA[7];       // +0x08 1
    std::uint32_t valuesB[7];       // +0x24 2
    std::uint32_t vitalMask;        // +0x40 3 the slot's vital mask (UnitStatSlot::vitalMask)
};
static_assert(offsetof(UnitLevelRecord, valuesB) == 0x24 && sizeof(UnitLevelRecord) == 0x44);

// unit_db_destroy.cpp (A-U11): the prototype, NPC and model records' destructors (0053EBC0,
// 0053ED30, 0053EE70).
namespace unitdb {
void __fastcall DestroyPrototypeRecord(UnitPrototypeRecord* record);
void __fastcall DestroyNpcRecord(UnitNpcRecord* record);
void __fastcall DestroyModelRecord(UnitModelRecord* record);
} // namespace unitdb

// The model table (0x20 bytes, the database's +0x40): the records (an STLport vector) and a
// hash_map<string, record*> from their names at +0x0C, as the prototype tables (unit_create.h).
struct UnitModelTable {
    using NameIndex = stlp::hash_map<stlp::string, UnitModelRecord*, stlp::StringHash>;

    // 0053D240 (RET 4; unit_db_load.cpp, A-U12; the database's load). The record named `name`:
    // operator[] on the name index, so an unknown name is inserted with a null record (and null
    // is returned).
    UnitModelRecord* FindByName(const char* name);

    UnitModelRecord* begin;
    UnitModelRecord* end;
    UnitModelRecord* endOfStorage;
    NameIndex byName;               // +0x0C
};
static_assert(offsetof(UnitModelTable, byName) == 0x0C && sizeof(UnitModelTable) == 0x20);

// The level record table (the database's +0x60), laid out the same way.
struct UnitLevelTable {
    using NameIndex = stlp::hash_map<stlp::string, UnitLevelRecord*, stlp::StringHash>;

    // 0053D650 (RET 4; unit_db_load.cpp, A-U12). As UnitModelTable::FindByName.
    UnitLevelRecord* FindByName(const char* name);

    UnitLevelRecord* begin;
    UnitLevelRecord* end;
    UnitLevelRecord* endOfStorage;
    NameIndex byName;               // +0x0C
};
static_assert(offsetof(UnitLevelTable, byName) == 0x0C && sizeof(UnitLevelTable) == 0x20);

// The unit database (global 007AF358): four 0x20-byte tables.
struct UnitDatabase {
    // 0053A8E0 (unit_sound_counts.cpp, RET 4). Lists the "*.wav" files of the sound system
    // `sounds` (006B7110), lower-cases them, then gives each model's sound names (lower-cased in
    // place) the number of files whose name starts with it (soundCounts, resized to the names).
    void CountSoundFiles(void* sounds);
    // 0053A200 (RET 8; unit_db_load.cpp, A-U12; Rebirth-patched: the vanilla behaviour). Reads
    // "units.udb" of the archive `folder` + `archive`: the level records (chunk 1), the models (2),
    // the prototypes (3) and the NPCs (4), each from its own pass over the file with every record
    // entered into its table's name index; then every prototype's modelIndex becomes the index of
    // the model named by modelName, and every model slot's levelIndex the index of the level
    // record named by levelName (-1 when there is none; the lookups leave a null entry for an
    // unknown name). Returns 1, or what the fatal error report returns when the archive or the
    // file cannot be opened.
    int Load(const char* folder, const char* archive);
    // 0057CBE0 (S13a, database_reset.cpp; the compiler's destruction and construction in place,
    // which the resource switch 0057CB50 runs). The tables from the last to the first, each the
    // name index (its nodes, then its buckets), then the records (in order) and their storage; then
    // the four from the first: no records, an index of 100 buckets (193).
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

// unit_stat_small.cpp (A-U12). 00539280 (thiscall on the model table's vector, RET 4) / 0053AD30
// (UnitPrototypeTable::At; thiscall on the prototype table, RET 4): record `index`, or null past the end.
UnitModelRecord* __fastcall UnitModelAt(UnitModelRecord** models, void*, std::uint32_t index);

// The skill (perk) database (global 007AF268; loaded from "perks.pdb" at startup by 00520220,
// Rebirth-patched): the skills, then the skill level records (three per skill: levels 1..3).
// unit_db.cpp (A-U10).

// A skill (0x18 bytes).
struct SkillRecord {
    // 0051F3A0 (RET 4). Fields 0..2 and chunk 10 (the stat names, InlineStrings).
    void Serialize(ChunkArchive* ar);
    // 00520D20 (S13; the compiler's destructor). The stat names and their storage, field1, name.
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
    // 0051F480 (RET 4). Fields 0, 1, 2, 10, 11, 12..22 and 0x17.
    void Serialize(ChunkArchive* ar);
    // 00520DE0 (S13; the compiler's destructor). The strings +0x14, +0x0C, +0x04, +0x00.
    void Destruct();

    mfc::CString field0;                // +0x00 field 0 (nothing reads it)
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

    // The level's value (values[7], unsigned), loaded as the original's FILD of a zero-extended
    // qword (exact).
    double Value() const { return x87::FromUnsigned(values[7]); }
    // The worth exponent the skill prices use (values[6], signed).
    int WorthExponent() const { return static_cast<int>(values[6]); }
};
static_assert(offsetof(SkillLevelRecord, values) == 0x18 && sizeof(SkillLevelRecord) == 0x44);

struct SkillDatabase {
    // 0051FE00 (RET 4). The level record named `levelName` (_mbsicmp), or null.
    SkillLevelRecord* FindLevel(const char* levelName);
    // 0051FE80 (RET 4). The index of the level record named `levelName` (_mbsicmp), or -1.
    int IndexOf(const char* levelName);
    // 0051FF00 (RET 4). The index of `record` among the level records, or -1.
    int IndexOfLevel(const SkillLevelRecord* record);
    // unit_stat_small.cpp (A-U12). 00526FF0 / 00524FA0 (RET 4): skill / level record `index`, or
    // null past the end.
    SkillRecord* SkillAt(std::uint32_t index);
    SkillLevelRecord* LevelAt(std::uint32_t index);
    // 00520220 (RET 8; unit_db_load.cpp, A-U12; Rebirth-patched: the vanilla behaviour). Reads
    // "perks.pdb" of the archive `folder` + `archive`: the skills (chunk 1) and the level records
    // (chunk 2, one chunk 1 each). Returns 1, or what the fatal error report returns when the
    // archive or the file cannot be opened.
    int Load(const char* folder, const char* archive);
    // 0057CD60 (S13; the compiler's destruction and construction in place, which the resource
    // switch 0057CB50 runs): the level records destroyed (00520DE0) and their storage freed, then
    // the skills (00520D20 through their deleting destructor 00521FA0 with 0); both vectors empty.
    void Reset();
    // 0051FB10 (database_construct.cpp; the static initialiser 0051FAF0). Both vectors empty.
    // Returns this.
    SkillDatabase* Construct();

    stlp::vector<SkillRecord> skills;        // +0x00
    stlp::vector<SkillLevelRecord> levels;   // +0x0C
};
static_assert(sizeof(stlp::vector<SkillRecord>) == 0x0C);
// The skill database (007AF268).
inline SkillDatabase* SkillDb() { return &ei::GameData<SkillDatabase>(0x007AF268); }
