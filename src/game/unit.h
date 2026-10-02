#pragma once

// Units (creatures; GetClassName "CUnit", class id 0x50, vtable 0073EA8C; the CFakeUnit vtable
// 0073BFE8 derives from it). Field and method names are provisional, from how the code uses them.
// The AI map (ai_map.h) and the sector map use the position, sector cell and size class fields.

#include "engine_object.h"
#include "item_db.h"

#include <cstddef>
#include <cstdint>

constexpr int kClassUnit = 0x50;    // CEngineObject::GetClassId() of units (creatures)

// CEngineObject::type_ of the player's own units (other kinds: 0x33 ...).
constexpr int kUnitTypeHero = 0x32;

// Class id of the unit's controller (CUnit + 0x240) for the units in a player's party
// (GetClassName "CUnitClientSpecific"); their items live in the player's inventory records.
constexpr int kClassPartyController = 0x3D;

// An entry of a unit's active effect list (+0x198, 0x14 bytes). The low byte of `kind` is the
// effect type (0x20: one that stops spell casting).
struct UnitEffect {
    std::uint32_t kind;
    std::uint32_t unknown_04[4];
};
static_assert(sizeof(UnitEffect) == 0x14);

// The record the player keeps for each unit of the party (0x784 bytes, found by unit id through
// 006621B0); only the item lists are read here.
struct PartyUnitRecord {
    std::uint8_t unknown_000[0x730];
    CEngineObject** weapons;        // +0x730
    std::uint32_t weaponCount;      // +0x734
    std::uint8_t unknown_738[8];
    CEngineObject** quickItems;     // +0x740 (an entry counts only with +0x48 > 0)
    std::uint32_t quickItemCount;   // +0x744
    std::uint8_t unknown_748[0x18];
    CEngineObject** spells;         // +0x760
    std::uint32_t spellCount;       // +0x764
};
static_assert(offsetof(PartyUnitRecord, weapons) == 0x730);
static_assert(offsetof(PartyUnitRecord, quickItems) == 0x740);
static_assert(offsetof(PartyUnitRecord, spells) == 0x760);

// The player's list of party unit records (player + 0x70).
struct PartyRecords {
    // 006621B0. The record of the unit with id `unitId`, or null (not rewritten).
    PartyUnitRecord* Find(int unitId);
};

class CUnit : public CEngineObject {
public:
    // 0050E930. Called when the unit leaves the sector map; not rewritten (sector_map.cpp).
    void OnLeftSectorMap();

    // 0050E9F0. Records that part `part` (0..15; negative: none) of the object changed, marks it
    // dirty (flags_ bit 1) and stamps it with the world's change counter. Returns what the original
    // leaves in EAX (0, or the world when the object is referenced). A method of the base class of
    // all map objects (it touches nothing beyond +0x1DE); declared here until that class exists.
    std::uintptr_t MarkChanged(int part);

    // 0050F630. Has the object an active effect of type `type` (low byte of UnitEffect::kind)?
    // Also a map object base method.
    BOOL HasEffect(std::uint32_t type);

    // 0052E940. Adds `mask` to the hostile mask (relationsA); marks part 11 changed when it grows.
    // Returns the new mask. (0052E970 sets the mask and tells the controller; not rewritten.)
    std::uintptr_t AddHostileMask(std::uint32_t mask);
    // 00530180. Sets relationsB; marks part 11.
    std::uintptr_t SetRelationsB(std::uint32_t mask);
    // 00530120. Sets the side; flags_ bit 0x80000; marks part 11 when the unit has a controller.
    std::uintptr_t SetSide(int side);
    // 00530150. Non-zero when this unit regards `other`'s side as hostile (the bit of other's
    // side in both relation masks).
    std::uint32_t IsHostileTo(const CUnit* other);

    // 0052E9B0. Sets unknown_438; marks part 13.
    std::uintptr_t SetUnknown438(int value);
    // 0052EA90. Sets the bytes at +0x254, +0x255, +0x256 and the int at +0x258; marks part 8.
    std::uintptr_t SetUnknown254(std::int8_t a, std::int8_t b, int c, std::int8_t d);
    // 0052EAD0 / 0052EAF0. Set unknown_25C / unknown_260; mark part 7 when they change.
    std::uintptr_t SetUnknown25C(int value);
    std::uintptr_t SetUnknown260(int value);
    // 0052EB10. Sets both movement modes (+0x264, +0x268); marks part 6.
    std::uintptr_t SetMoveModes(int mode, int mode2);
    // 0052EBE0. Clears (on) or sets (off) flags_ bit 0x40000000; marks part 5.
    std::uintptr_t SetFlag40000000Off(bool on);
    // 0052F7F0. Virtual slot 46: sets or clears flags_ bit 0x1000000. Returns the new flags.
    std::uint32_t SetFlag1000000(int on);

    // 0052EC10. Writes the item ids (+0x18) of the 4 weapons and then of the 7 armour pieces to
    // `out` (-1 for an empty slot).
    void GetEquipmentIds(int* out);

    // 0052F820. Can the unit cast the spell in slot `slot` (0..7)? 0 yes, 1 not enough mana, 3 no
    // spell, 4 an effect of type 0x20 prevents it. The second argument is not used.
    int CanCastSpell(std::uint32_t slot, int unused);
    // 0052F8B0. Can the quick item in slot `slot` be used? 3 none or not a spell carrier, 1 a
    // category 5 item with too little charge, else 0. The second argument is not used.
    int CanUseQuickItem(std::uint32_t slot, int unused);

    // 0052F920. Movement speeds for the current movement mode (+0x264): `forward` and `backward`
    // (the latter scaled by the global 007C5438).
    void GetMoveSpeeds(float* forward, float* backward);
    // 0052FA20. The speed of movement mode 2 (+0x268).
    float MoveSpeed2();
    // 0052FAF0. The speed of movement mode `mode` (0..4; other values: the base speed).
    float MoveSpeedFor(int mode);
    // 0052FBC0. 3 * value + 10 for units without a player (value capped at 10 when flags_ has
    // 0x4000000), else 1000000.
    float ScaledValue(float value);

    // 005302F0. The base speed (BaseSpeed) scaled by the movement mode: x0.2 when standing or
    // inactive, x0.7 in mode 1.
    float ModeSpeed();
    // 00530370. 1.8 * +0x14C when flag 2 of +0x124 is set, else 0.7 * +0x134 + +0x130.
    float BaseSpeed();

    // 005307B0. Sum of +0x34 (weight) over all carried items.
    float TotalItemWeight();

    // 00530930. The unit's prototype record (index unknown_18), or null.
    char* GetPrototype();
    // 00530970. The record (0x168 bytes) the prototype's +0x08 indexes, or null.
    char* GetPrototypeModel();
    // 005309E0. The prototype record of index unknown_23C, or null.
    char* GetPrototype23C();
    // 005308A0. Stores the index of prototype record `record` in unknown_23C.
    // Returns what the original leaves in EAX (1 when `record` lies before the table).
    std::uint32_t SetPrototype23C(const char* record);

    // 00530FA0. Is the active weapon (+0x26C) of type 5 or 6 (ranged)? Returns AL only.
    bool HasRangedWeapon();

    // 00531070 / 00531180 / 00531820. Put an item into the first free weapon (4), quick item (4)
    // or spell (8) slot and take a reference: returns the slot, 1 if already there (sic), -1 if
    // full, 0 for null (weapons and quick items only).
    int AddWeapon(CEngineObject* item);
    int AddQuickItem(CEngineObject* item);
    int AddSpell(CEngineObject* item);
    // 005310D0 / 005311E0. Empty weapon / quick item slot `slot` (releasing the item); part 5.
    // Return what the original leaves in EAX (the slot when out of range).
    std::uintptr_t RemoveWeapon(std::uint32_t slot);
    std::uintptr_t RemoveQuickItem(std::uint32_t slot);

    // 005318B0 / 00531920 / 005319A0. The weapon / quick item / spell in slot `slot`, from the
    // player's party record for party units. Null when out of range.
    CEngineObject* GetWeapon(std::uint32_t slot);
    CEngineObject* GetQuickItem(std::uint32_t slot);
    CEngineObject* GetSpell(std::uint32_t slot);
    // 00531A10. The CSpell (+0x44) of the spell item in slot `slot`, or null.
    void* GetSpellObject(std::uint32_t slot);

    // 005327D0. Resistance factor against the damage type in bits 18-21 of hit + 0x14.
    float ResistanceTo(const void* hit);

    // 00532DA0. Virtual slot 18: for hero units, whether `target` + 0x18 has the bits the
    // controller asks for (0x10 by its slot 36, 0x20 by slot 37); true for other units.
    bool AcceptsTarget(const void* target);

    // 00532E00. Ratio of the 00518D30 values at +0xCC and +0x1B4.
    float AnimationRatio();

    // Not rewritten (forwarders):
    // 00455950. The world the object lives in (owner_ + 0xAC, its slot 13), or null.
    void* World();
    // 00518D30. An animation time factor (1 for no animation).
    float AnimationFactor(float time);

    float positionX;                // +0x01C
    float positionY;                // +0x020
    float positionZ;                // +0x024
    std::uint8_t unknown_028[0xA4 - 0x28];
    const char* prototypeName;      // +0x0A4 e.g. "unhuma"
    std::uint8_t unknown_0A8[0xCC - 0xA8];
    float animationCC;              // +0x0CC an animation time
    std::uint8_t unknown_0D0[0x124 - 0xD0];
    std::uint8_t speedFlags;        // +0x124 bit 1: use speed14C
    std::uint8_t unknown_125[0x130 - 0x125];
    float speed130;                 // +0x130
    float speed134;                 // +0x134
    std::uint8_t unknown_138[0x14C - 0x138];
    float speed14C;                 // +0x14C
    std::uint8_t unknown_150[0x17E - 0x150];
    std::int16_t hitPoints;         // +0x17E
    std::int16_t maxHitPoints;      // +0x180
    std::uint16_t mana;             // +0x182
    std::uint8_t unknown_184[0x198 - 0x184];
    UnitEffect* effects;            // +0x198
    int effectCount;                // +0x19C
    std::uint8_t unknown_1A0[0x1B4 - 0x1A0];
    float animation1B4;             // +0x1B4 an animation time
    int sectorCellX;                // +0x1B8 cell the unit is registered in, -1 when not
    int sectorCellY;                // +0x1BC
    int changeStamp;                // +0x1C0 world + 0x68 when last changed
    std::uint8_t unknown_1C4[0x1DC - 0x1C4];
    std::uint16_t changedParts;     // +0x1DC bit per part changed (MarkChanged)
    std::uint8_t unknown_1DE[0x23C - 0x1DE];
    int prototype23C;               // +0x23C a second prototype index (-1 none)
    CEngineObject* controller;      // +0x240 (the script code calls it the perception object)
    std::uint32_t relationsA;       // +0x244 \ a unit regards side s as hostile when both have
    std::uint32_t relationsB;       // +0x248 / bit s set
    char* player;                   // +0x24C the player that owns the unit (null: none)
    int side;                       // +0x250
    std::int8_t unknown_254;
    std::int8_t unknown_255;
    std::int8_t unknown_256;
    std::uint8_t unknown_257;
    int unknown_258;
    int unknown_25C;
    int unknown_260;
    int moveMode;                   // +0x264 0..4
    int moveMode2;                  // +0x268
    std::uint32_t activeWeapon;     // +0x26C slot of the weapon in use
    std::uint8_t unknown_270[0x324 - 0x270];
    int sizeClass;                  // +0x324 selects the 4-bit passability field in each AI map cell
    std::uint8_t unknown_328[4];
    float standingHeight;           // +0x32C; deeper water or higher ground than (2 - height) * 16 blocks it
    std::uint8_t unknown_330[0x33E - 0x330];
    std::uint8_t attackRate;        // +0x33E (25 / value: see UnitParamsPeriod)
    std::uint8_t movementKind;      // +0x33F 2: moves on the water surface
    std::uint8_t modeSpeeds[4];     // +0x340 speed bytes of movement modes 3 (and 2's backward), 2, 1, 0
    std::uint8_t baseSpeed;         // +0x344
    std::uint8_t unknown_345[0x438 - 0x345];
    int unknown_438;                // +0x438
    std::uint32_t unknown_43C;
    CEngineObject* weapons[4];      // +0x440
    CEngineObject* quickItems[4];   // +0x450
    CEngineObject* spells[8];       // +0x460
    CEngineObject* armor[7];        // +0x480
    std::uint32_t unknown_49C;
    CEngineObject** carried;        // +0x4A0 CArray of further items
    int carriedCount;               // +0x4A4
    std::uint8_t unknown_4A8[0x4B0 - 0x4A8];
    float resistances[6];           // +0x4B0 per damage type (see ResistanceTo)
    std::uint8_t unknown_4C8[0x4DC - 0x4C8];
};
static_assert(offsetof(CUnit, prototypeName) == 0xA4);
static_assert(offsetof(CUnit, speedFlags) == 0x124);
static_assert(offsetof(CUnit, speed14C) == 0x14C);
static_assert(offsetof(CUnit, hitPoints) == 0x17E);
static_assert(offsetof(CUnit, mana) == 0x182);
static_assert(offsetof(CUnit, effects) == 0x198);
static_assert(offsetof(CUnit, sectorCellX) == 0x1B8);
static_assert(offsetof(CUnit, changedParts) == 0x1DC);
static_assert(offsetof(CUnit, controller) == 0x240);
static_assert(offsetof(CUnit, side) == 0x250);
static_assert(offsetof(CUnit, moveMode) == 0x264);
static_assert(offsetof(CUnit, activeWeapon) == 0x26C);
static_assert(offsetof(CUnit, sizeClass) == 0x324);
static_assert(offsetof(CUnit, standingHeight) == 0x32C);
static_assert(offsetof(CUnit, modeSpeeds) == 0x340);
static_assert(offsetof(CUnit, unknown_438) == 0x438);
static_assert(offsetof(CUnit, weapons) == 0x440);
static_assert(offsetof(CUnit, armor) == 0x480);
static_assert(offsetof(CUnit, carried) == 0x4A0);
static_assert(offsetof(CUnit, resistances) == 0x4B0);
static_assert(sizeof(CUnit) == 0x4DC);    // operator new(0x4DC) in 00512730

constexpr std::uint32_t kInSectorMap = 0x8000;    // CEngineObject::flags_

// The unit's parameter block (CUnit + 0x330); only the byte read by 005271C0.
struct UnitParams {
    // 005271C0. 25 / attackRate.
    float Period();

    std::uint8_t unknown_00[0x0E];
    std::uint8_t attackRate;        // +0x0E
};

// Unit prototypes (global vectors; see item_db.h for the lookup rule).
namespace unitdb {
constexpr int kPrototypeSize = 0x120;
constexpr int kModelSize = 0x168;
constexpr int kSkillSize = 0x18;
constexpr int kSkillLevelSize = 0x44;
inline const RecordTable& Prototypes() { return *reinterpret_cast<const RecordTable*>(0x007AF358); }
inline const RecordTable& Models() { return *reinterpret_cast<const RecordTable*>(0x007AF398); }
inline const RecordTable& Skills() { return *reinterpret_cast<const RecordTable*>(0x007AF268); }
inline const RecordTable& SkillLevels() { return *reinterpret_cast<const RecordTable*>(0x007AF274); }
} // namespace unitdb

// One of the six slots of UnitStats (0xF4 bytes); only the tail fields are read here.
struct UnitStatSlot {
    std::uint8_t unknown_00[0xE0];
    int kind;                       // +0xE0 0 unused; 1 a proportional reduction
    float current;                  // +0xE4
    float maximum;                  // +0xE8
    float unknown_EC;
    float factor;                   // +0xF0
};
static_assert(sizeof(UnitStatSlot) == 0xF4);

// A creature's computed statistics (0x704 bytes; built from the unit's parameters, often
// as a stack copy). Only the fields read here; names provisional.
struct UnitStats {
    // 00523CD0. 25 / unknown_20.
    float Period20();
    // 00525DF0. Attribute byte `index` (+0x6C8) as a float.
    float AttributeBase(int index);
    // 00525E10. Attribute `index` (0..2) with the bonus of the stat it depends on (stat - 25).
    float AttributeWithBonus(int index);
    // 00527030. Skill byte `index` (+0x6CB) as a float.
    float SkillBase(int index);
    // 00527050. Skill `index` plus (stat - 25) of the stat its name starts with ('s', 'd', 'i').
    float SkillWithBonus(std::uint32_t index);
    // 00526780. The weapon skill for weapon type `type`.
    float WeaponSkill(int type);
    // 005267E0. +0x34 of the level record of skill `index` (0 without one), exact in ST0.
    double SkillLevelValue(int index);
    // 00528270. The level record of skill `index` (level byte at +0x6D3), or null.
    char* SkillLevelRecord(int index);
    // 00524280. stat10 * 12 scaled by (1 + 0.01 * level value) of the level byte at +0x6E8.
    float Value6E8();
    // 00525CD0. global 007C5430 + 0.01 * level value of the byte at +0x6E9.
    float Value6E9();
    // 00525D60. The same with an unused argument (RET 4).
    float Value6E9Arg(int unused);
    // 005259B0. `value` minus what the six slots take from it (by their kind, current, maximum
    // and factor).
    float Reduce(float value);
    // 00524020. `value` minus value * factor of each slot record of kind 1.
    float ReduceKind1(float value);
    // 00528840. Two factors from stat10 and stat18 (relative to 15, over the global 007AF280).
    // The third argument is not used.
    void GetFactors(float* a, float* b, int unused);

    std::uint8_t unknown_000[0x10];
    float stat10;                   // +0x010 's'
    float unknown_14;
    float stat18;                   // +0x018 'd'
    float unknown_1C;
    float stat20;                   // +0x020 'i'
    std::uint8_t unknown_024[0x110 - 0x24];
    UnitStatSlot slots[6];          // +0x110
    std::uint8_t attributes[3];     // +0x6C8
    std::uint8_t skills[8];         // +0x6CB
    std::uint8_t skillLevels[0x25]; // +0x6D3 1-based level per skill, 0 none
    float unknown_6F8[3];           // +0x6F8 (+0x700 accumulates experience?)
};
static_assert(offsetof(UnitStats, slots) == 0x110);
static_assert(offsetof(UnitStats, attributes) == 0x6C8);
static_assert(offsetof(UnitStats, skillLevels) == 0x6D3);
static_assert(sizeof(UnitStats) == 0x704);

// A key and the lock it is tested against (levers and traps keep their lock at +0x244):
// {mask, id, level}. Names provisional.
struct LockKey {
    // 00522A80. Do the masks overlap, and either (bit 3 in common) the ids match, or (bits 0-2 in
    // common) this level is at least the lock's (unsigned)?
    BOOL Opens(const LockKey* lock);

    std::uint32_t mask;
    std::uint32_t id;
    std::uint32_t level;
};

struct DamageInfo;   // spell.h
// 00522B40 (thiscall). Resets a hit: all weights 1, multiplier 1, +0x20/+0x24 = 3.4e36, +0x2C = 6,
// the rest 0. Returns what the original leaves in EAX (0x7C23D70A).
std::uint32_t __fastcall ResetDamageInfo(DamageInfo* info);
// 00522B80 (thiscall). Has the hit a non-zero weight for damage type 3, 4 or 5 (NaN counts as
// zero)?
BOOL __fastcall HasElementalDamage(const DamageInfo* info);

// The server-side state object of map objects (GetClassName "CMapObjectServer", class id 0x39,
// vtable 0073DD6C). Only the hit points.
class CMapObjectServer : public CEngineObject {
public:
    // 005156A0. Virtual slot 17: hit points rounded (FISTP) to a 16-bit value are <= 0.
    BOOL IsDead();
    // 005156C0. Virtual slot 18: hit points below -5 times the maximum.
    BOOL IsOverkilled();

    std::uint8_t unknown_1C[0x28 - 0x1C];
    float hitPoints;        // +0x28
    float maxHitPoints;     // +0x2C
};
static_assert(offsetof(CMapObjectServer, hitPoints) == 0x28);

// The client-side companion of a map object (GetClassName "CMapObjectClientSpecific", vtable
// 0073DBEC); +0x1C is the object it belongs to (a unit for these two).
class CMapObjectClientSpecific : public CEngineObject {
public:
    // 005104D0. Virtual slot 17: the unit's hit points <= 0.
    BOOL IsDead();
    // 005104F0. Virtual slot 18: the unit's hit points below -5 times its maximum.
    BOOL IsOverkilled();

    CUnit* object;          // +0x1C
};
