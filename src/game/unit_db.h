#pragma once

// The records of the units database (units.udb: the prototype, NPC, model and skill level tables) and how each one is
// read from / written to the tagged-chunk archive (model.h, ChunkArchive); the field ids are the archive's. See
// docs/notes/units.md ("Database records").

#include "ei/x87.h"
#include "game_data.h"
#include "lib/mfc.h"
#include "maths.h"

#include "lib/stlp.h"
#include "lib/static_object.h"
#include "unit_create.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>

class ChunkArchive;

// Serialised as chunk `id` holding one field 1 per string.
using CStringVector = stlp::vector<mfc::CString>;
static_assert(sizeof(CStringVector) == 0xC);

// A vector<CString> or vector<std::string> as chunk `id` holding one field 1 per string, the way the NPC
// and quest briefing serialisers inline it: loading empties the vector (the other serialiser erases the range
// instead, with the same result) and resizes it to the count of fields 1; each string is the next occurrence
// of field 1.
template <typename Text>
void InlineStrings(ChunkArchive* ar, int id, stlp::vector<Text>* strings);

// One gold roll of a prototype's loot: an amount between `minimum` and `maximum`, dropped when a percent roll
// is within `chance`.
struct UnitGoldRoll {
    float chance;                   // percent
    std::uint32_t field1;               // field 1 (nothing reads it)
    float minimum;
    float maximum;

    // The four dwords as fields 0..3 (the prototype's second roll, chunk 0x1C).
    void Serialize(ChunkArchive* ar);
};
static_assert(sizeof(UnitGoldRoll) == 0x10);

struct UnitPrototypeRecord {
    UnitPrototypeRecord* DeleteScalar(unsigned flags);
    void Serialize(ChunkArchive* ar);

    std::string name;              // field 0
    std::string modelName;         // 1
    int modelIndex;                 // 2 (replaced by the model's index at load)
    int skin;                       // 3
    int hair;                       // 4 (-1: none)
    Vector3 scale;                  // 5, 6, 7
    std::uint32_t panelViewSize;        // 0x22 the unit panel's model view size
    std::uint32_t height;               // 0x23 (float bits) its height: the panel view, reach and small-unit checks
    float viewDistance;             // 0x25 how far before the unit a dialogue's camera may stand
    float viewHeight;               // 0x26 the height of its eyes for a dialogue's camera
    float shake;                    // 0x28 the camera shake of its steps and attacks
    std::string soundFolder;       // 8 (when set, the action sounds come from here, not the model's)
    std::uint8_t unused_38[0x10];  // not serialised
    float hitPoints;                // 9
    float mana;                     // 10
    std::uint32_t level;            // 0x29 (CUnit::Level of a unit without a text key)
    std::uint32_t experienceValue;      // 0x2B the experience its death awards
    float protectionScale;          // 11
    std::uint32_t field12;              // 12 (UnitStats::attackCategory)
    float speed;                    // 13
    float combat[5];                // 14, 15, 0x24, 16, 17
    std::uint32_t naturalWeapon;    // 0x12
    std::uint32_t hitSoundKind;     // 0x27 (low byte) the weapon class of its hits: the hit sound class
    float damage;                   // 0x13
    float damageSpread;             // 0x14
    float skillLevel;               // 0x15 every skill and attribute byte starts here (rounded)
    float attributes01;             // 0x16 attributes 0 and 1
    float attribute2;               // 0x17 attribute 2
    std::uint32_t peripheralRange;      // 0x18 its peripheral sight range
    float valuesB0[6];              // 0x19
    float valuesE0[6];              // 0x1A
    UnitGoldRoll gold;              // chunk 0x1B, fields 0..3 (the loot)
    UnitGoldRoll secondGold;        // chunk 0x1C (fields 0..3)
    CStringVector lootItems;            // 0x1D item names its loot draws from
    float skills3to5[3];            // 0x1E
    // The starting equipment of a new unit: spells for every unit; armour pieces and two weapons (item names,
    // may be empty) for units of type 0x32.
    CStringVector spellNames;       // 0x1F
    CStringVector armourNames;      // 0x20
    std::string weaponName;        // 0x21
    std::string secondWeaponName;  // 0x2A
};

// A named creature's statistics (found by the prototype name).
struct UnitNpcRecord {
    void Serialize(ChunkArchive* ar);

    std::string name;              // field 0 (the prototype's name)
    std::uint32_t field1;               // 1 (nothing reads it)
    float experience;               // 2
    float stats[3];                 // 3, 4, 5 (strength, dexterity, intelligence)
    std::uint8_t attributes[3];     // 6 (three bytes)
    std::uint8_t skills[8];         // 7 (eight bytes)
    std::uint8_t unused_23;        // not serialised
    CStringVector skillNames;       // 8 (the skill level records it learns)
    CStringVector startWeapons;     // 9: item names a multiplayer hero starts with
    CStringVector startQuickItems;  // 10: its quick items
    CStringVector startSpells;      // 11: its spells
    float experienceFloor;          // 12: its starting experience, which it keeps when it dies in a network game
    int startMoney;                 // 13: money its party starts with
    std::uint8_t voiced;                // 14 (one byte): the multiplayer hero screen offers it as a voice
    std::uint8_t unused_5D[3];
};

// One of a model's six body slots (serialised as fields 0..3 of chunk 0x0C + slot): the skill
// level record's name, its index (resolved at load), the slot's share and factor.
struct UnitModelSlot {
    // The level name as field 0, then levelIndex, share and factor as 1..3.
    void Serialize(ChunkArchive* ar);

    std::string levelName;
    int levelIndex;
    float share;
    float factor;
};

struct UnitModelRecord {
    void Serialize(ChunkArchive* ar);

    std::string name;              // field 0
    int objectClass;                // 1
    float hitPointRegen;            // 2
    float manaRegen;                // 3
    std::uint32_t field4;               // 4 (nothing reads it)
    std::uint32_t movementKind;         // 5 (UnitStats::movementKind)
    float angle;                    // 6 (degrees)
    std::uint32_t modeSpeeds[4];    // 7
    std::uint32_t actionSpeeds[6];  // 0x24 (float bits; copied to CUnit::actionSpeeds)
    std::uint32_t field8;               // 8 (UnitStats::modelField8; nothing reads it)
    std::uint32_t sizeClass;        // 9
    std::uint32_t sizeClassSmall;   // 10
    std::uint32_t damageWeights[7]; // 11
    float protection[7];            // 0x27
    UnitModelSlot slots[6];         // chunks 0x0C..0x11
    std::string figureName;            // 0x1E the figure and skin file prefix ("%sskin_%.2d.mmp")
    CStringVector field1F;              // 0x1F (nothing reads it)
    CStringVector field20;              // 0x20 (nothing reads it)
    float height;                   // 0x21 (the object stands this much lower)
    std::uint32_t bloodKind;        // 0x28 1..4 blood (particles 0x200F / 0x2031..0x2033 and a decal), 5..8 particles only
    int footprintKind;              // 0x2A the footprint texture (-1: none)
    std::uint32_t footPart;         // 0x2B the foot parts "ll<n>" / "rl<n>" (1..8)
    std::uint32_t castEffect;           // 0x29 nonzero: its casts show an effect
    std::string hitMaterial;       // 0x2C the material its hit sounds use ("stone", "leather", ...)
    std::uint8_t swapFeet;          // 0x2D (one byte) the first step is the right foot
    std::uint8_t unused_11D[3];
    std::uint32_t sizeScale;            // 0x2E (float bits) multiplies the unit's size for its size classes
    std::string soundFolder;       // 0x22 (the step / attack / idle sounds: "<folder>\idle\<n>.wav" ...)
    std::uint8_t unused_128[0x10]; // not serialised
    CStringVector soundNames;       // 0x23 (the .wav files of each are counted)
    // The number of sound files of each soundNames entry (counted at startup; not serialised).
    stlp::vector<std::uint32_t> soundCounts;

    const std::uint32_t* SoundCounts() const { return soundCounts.begin(); }
    std::uint32_t idleSoundChance;  // 0x25 percent
    std::uint32_t attackSoundChance;    // 0x26 percent
    // The eyes on the skin texture (in 256ths of its width; the blink; read as signed ints): left, distance
    // of their bottom from the bottom edge, width, height.
    std::uint32_t eyeLeft;          // 0x2F
    std::uint32_t eyeBottom;        // 0x30
    std::uint32_t eyeWidth;         // 0x31
    std::uint32_t eyeHeight;        // 0x32
};

// A unit database level record (the fourth table).
struct UnitLevelRecord {
    UnitLevelRecord* DeleteScalar(unsigned flags);
    // The name as field 0, valuesA / valuesB as fields 1 / 2, vitalMask as 3 and slotType as 4.
    void Serialize(ChunkArchive* ar);

    std::string name;              // field 0
    int slotType;                   // 4 the type a model's body slot of this level gets (UnitStatSlot::type)
    std::uint32_t valuesA[7];       // 1
    std::uint32_t valuesB[7];       // 2
    std::uint32_t vitalMask;        // 3 the slot's vital mask (UnitStatSlot::vitalMask)
};

namespace unitdb {
void __fastcall DestroyPrototypeRecord(UnitPrototypeRecord* record);
void __fastcall DestroyNpcRecord(UnitNpcRecord* record);
void __fastcall DestroyModelRecord(UnitModelRecord* record);
void __fastcall DestroyModelSlot(UnitModelSlot* slot);
void __fastcall DestroyLevelRecord(UnitLevelRecord* record);
void __fastcall DestroyStringRange(mfc::CString* first, mfc::CString* last);
} // namespace unitdb

// The model table: the records and an index by name, as the prototype tables.
struct UnitModelTable {
    using NameIndex = stlp::hash_map<stlp::string, UnitModelRecord*, stlp::StringHash>;

    // The record named `name`: operator[] on the name index, so an unknown name is
    // inserted with a null record (and null is returned).
    UnitModelRecord* FindByName(const char* name);
    UnitModelRecord* At(std::uint32_t index);

    UnitModelRecord* begin;
    UnitModelRecord* end;
    UnitModelRecord* endOfStorage;
    NameIndex byName;
};
static_assert(offsetof(UnitModelTable, byName) == 0x0C && sizeof(UnitModelTable) == 0x20);

// The level record table, laid out the same way.
struct UnitLevelTable {
    using NameIndex = stlp::hash_map<stlp::string, UnitLevelRecord*, stlp::StringHash>;

    // As UnitModelTable::FindByName.
    UnitLevelRecord* FindByName(const char* name);

    UnitLevelRecord* begin;
    UnitLevelRecord* end;
    UnitLevelRecord* endOfStorage;
    NameIndex byName;
};
static_assert(offsetof(UnitLevelTable, byName) == 0x0C && sizeof(UnitLevelTable) == 0x20);

struct UnitDatabase {
    // Gives each model's sound names (lower-cased in place) the number of "*.wav" files of the sound system `sounds`
    // whose name starts with it (soundCounts).
    void CountSoundFiles(void* sounds);
    // Rebirth-patched: the vanilla behaviour. Reads "units.udb" of the archive `folder` + `archive`: the level
    // records (chunk 1), models (2), prototypes (3) and NPCs (4), each from its own pass with every record entered
    // into its table's name index; then every prototype's modelIndex and every model slot's levelIndex are resolved
    // by name (-1 when there is none). Returns 1, or what the fatal error report returns when the archive or the file
    // cannot be opened.
    int Load(const char* folder, const char* archive);
    // Destruction and construction in place, which the resource switch runs: every table emptied (index and
    // records freed), then the four constructed empty with an index of 100 buckets (193).
    void Reset();

    UnitPrototypeTable prototypes;
    NpcPrototypeTable npcs;
    UnitModelTable models;
    UnitLevelTable levels;
};
static_assert(offsetof(UnitDatabase, npcs) == 0x20);
static_assert(offsetof(UnitDatabase, models) == 0x40);
static_assert(offsetof(UnitDatabase, levels) == 0x60);
static_assert(sizeof(UnitDatabase) == 0x80);

extern lib::StaticObject<UnitDatabase> g_unitDatabase;
namespace unitdb {
inline UnitDatabase& Database() { return *g_unitDatabase; }
// The record named `name` (an unknown name gets a null entry), or null.
inline UnitPrototypeRecord* PrototypeNamed(const stlp::string& name) { return PrototypeTable().byName.Subscript(name); }
// The byte distance of `record` from the first prototype record: what the originals divide by
// 0x120 themselves (a null record included, which gives a negative distance).
inline int PrototypeByteOffset(const UnitPrototypeRecord* record) {
    return static_cast<int>(reinterpret_cast<const char*>(record) - reinterpret_cast<const char*>(PrototypeTable().begin));
}
inline UnitModelRecord* ModelNamed(const stlp::string& name) { return Database().models.byName.Subscript(name); }
} // namespace unitdb

// The skill (perk) database (loaded from "perks.pdb" at startup): the skills, then the skill level records
// (three per skill: levels 1..3).

struct SkillRecord {
    // Fields 0..2 and chunk 10 (the stat names, InlineStrings).
    void Serialize(ChunkArchive* ar);
    void Destruct();

    std::string name;                   // field 0
    std::string field1;                 // 1 (nothing reads it)
    std::uint32_t field2;               // 2 (nothing reads it)
    // Chunk 10: the stats it counts; the first one's first letter picks the stat ('s' strength, 'd'
    // dexterity, 'i' intelligence; UnitStats::SkillWithBonus).
    CStringVector statNames;
};

// A skill level record (the perks the trade panel offers).
struct SkillLevelRecord {
    // Fields 0, 1, 2, 10, 11, 12..22 and 0x17.
    void Serialize(ChunkArchive* ar);
    void Destruct();

    std::string field0;                 // field 0 (the trade panel's display name of the perk)
    std::string name;                   // 1 (the lookups compare it, _mbsicmp)
    std::uint32_t field2;               // 2 (nothing reads it)
    std::string requiredLevelName;      // 10 the level record it requires ("none")
    std::uint32_t exclusionGroup;       // 0x17 levels of one group exclude each other across skills (CanLearn)
    std::string requiredSkillName;      // 11 names the skill byte it requires ("none")
    // Fields 12..21: [0] the skill byte it requires (an index of UnitStats::skills, named by
    // requiredSkillName) and [2] its least value, [3..5] the least strength, dexterity and intelligence
    // (floats), [6] its worth exponent, [7] the level's value.
    std::uint32_t values[10];
    std::uint8_t learnable;         // 22 (one byte; UnitStats::CanLearn)
    std::uint8_t unused_41[3];

    int RequiredSkill() const { return static_cast<int>(values[0]); }
    std::uint32_t RequiredSkillValue() const { return values[2]; }
    // stat 0 strength, 1 dexterity, 2 intelligence.
    float RequiredStat(int stat) const { return std::bit_cast<float>(values[3 + stat]); }

    // The level's value (values[7], unsigned; exact).
    double Value() const { return x87::FromUnsigned(values[7]); }
    // The worth exponent the skill prices use (values[6], signed).
    int WorthExponent() const { return static_cast<int>(values[6]); }
};

struct SkillDatabase {
    SkillLevelRecord* FindLevel(const char* levelName);
    int IndexOf(const char* levelName) const;
    int IndexOfLevel(const SkillLevelRecord* record);
    // Rebirth-patched: the vanilla behaviour. Reads "perks.pdb" of the archive `folder` +
    // `archive`: the skills (chunk 1) and the level records (chunk 2, one chunk 1 each). Returns 1, or what
    // the fatal error report returns when the archive or the file cannot be opened.
    int Load(const char* folder, const char* archive);
    // Destruction and construction in place, which the resource switch runs: both vectors emptied.
    void Reset();
    SkillDatabase* Construct();

    stlp::vector<SkillRecord> skills;
    stlp::vector<SkillLevelRecord> levels;
};
static_assert(sizeof(stlp::vector<SkillRecord>) == 0x0C);
extern lib::StaticObject<SkillDatabase> g_skillDatabase;
inline SkillDatabase* SkillDb() { return &*g_skillDatabase; }
namespace unitdb {
inline SkillLevelRecord* SkillLevelAt(std::uint32_t index) {
    stlp::vector<SkillLevelRecord>& levels = SkillDb()->levels;
    return index < static_cast<std::uint32_t>(levels.end() - levels.begin()) ? levels.begin() + index : nullptr;
}
inline SkillRecord* SkillAt(std::uint32_t index) {
    stlp::vector<SkillRecord>& skills = SkillDb()->skills;
    return index < static_cast<std::uint32_t>(skills.end() - skills.begin()) ? skills.begin() + index : nullptr;
}
inline UnitLevelRecord* ModelLevelAt(std::uint32_t index) {
    UnitLevelTable& levels = Database().levels;
    return index < static_cast<std::uint32_t>(levels.end - levels.begin) ? levels.begin + index : nullptr;
}
} // namespace unitdb
