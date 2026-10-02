#pragma once

// Combat: the server-side unit controller (GetClassName "CUnitServer", class id 0x3E, vtable
// 0073EF38; its abstract base has vtable 0073EDBC). Every creature on the server has one at
// CUnit + 0x240; it keeps the unit's computed statistics, its current orders, and the attack /
// hit code. Field and method names are provisional, from how the code uses them.

#include "spell.h"
#include "unit.h"

#include <cstddef>
#include <cstdint>

constexpr int kClassUnitServer = 0x3E;

// An order of the unit (0x24 bytes; CUnitServer + 0x190 and + 0x1B4). 0054B230 resets one (kind 9,
// releasing the target).
struct UnitAction {
    int kind;                   // +0x00 3 attack, 4/5 cast (spell or quick item), 6 use/skill, 9 none
    std::uint32_t source;       // +0x04 spell slot, or quick item slot | 0x80000000; 0x18A95 for a
                                //       skill used on a target (kind 6)
    CEngineObject* target;      // +0x08 (referenced)
    float point[3];             // +0x0C target point
    std::uint8_t flag18;        // +0x18
    std::uint8_t unknown_19[3];
    int unknown_1C;
    std::uint8_t unknown_20;
    std::uint8_t unknown_21[3];
};
static_assert(sizeof(UnitAction) == 0x24);

// A movement mode and whether it was mode 3 (CUnitServer + 0x168 / + 0x170, 8 bytes each).
struct MoveModeState {
    int mode;                   // +0x00 0..2 (3 is stored as 2 with `wasMode3`)
    std::uint8_t flag4;         // +0x04 read by 005500A0 for mode 2
    std::uint8_t wasMode3;      // +0x05
    std::uint8_t unknown_6[2];
};
static_assert(sizeof(MoveModeState) == 8);

// What a unit attacks with (0x20 bytes), filled by CUnitServer::GetAttackParams (vtable slot 50).
// Floats and ints mixed, as the original stores them.
struct AttackParams {
    int weaponType;             // +0x00 CItemWeapon::GetType, or the natural weapon (stats +0xAC)
    int damageType;             // +0x04 0..6
    float speed;                // +0x08 item +0x34 / stats +0x80
    float damageMin;            // +0x0C scaled by the stats' damage scale (00523D30)
    float damageMax;            // +0x10 damage + spread, scaled likewise
    float attackSpread;         // +0x14 stats +0x78: random range of the attack roll
    int attack;                 // +0x18 attack skill (truncated)
    int defence;                // +0x1C defence skill (truncated; 0 for ranged weapons)
};
static_assert(sizeof(AttackParams) == 0x20);

// The combat values at the start of UnitStats (unit.h leaves them unnamed): an overlay of its
// first 0x110 bytes. Offsets are relative to UnitStats (CUnitServer + 0x1E0).
struct CombatStats {
    std::uint8_t unknown_00[4];
    float unknown_04;           // +0x04 log-scaled into the level (00556760)
    std::uint8_t unknown_08[0x30 - 0x08];
    float unknown_30;           // +0x30 its ratio 15 / value scales item values (0054B760)
    std::uint8_t unknown_34[0x70 - 0x34];
    float reach;                // +0x70 attack range without a weapon
    float attack;               // +0x74
    float attackSpread;         // +0x78
    float defence;              // +0x7C
    float speed;                // +0x80
    float damage;               // +0x84
    float damageSpread;         // +0x88
    float unknown_8C;           // +0x8C
    float damageWeights[7];     // +0x90 natural attack, per damage type
    int naturalWeapon;          // +0xAC weapon type of the natural attack (0x10: none / spells only)
    std::uint8_t unknown_B0[0x110 - 0xB0];
};
static_assert(offsetof(CombatStats, reach) == 0x70);
static_assert(offsetof(CombatStats, damageWeights) == 0x90);
static_assert(offsetof(CombatStats, naturalWeapon) == 0xAC);
static_assert(sizeof(CombatStats) == 0x110);

class CItem;

class CUnitServer : public CEngineObject {
public:
    CombatStats& Combat() { return *reinterpret_cast<CombatStats*>(&stats); }

    // 0054B620. Attack range: the active weapon's +0x58, else the natural reach.
    float AttackRange();
    // 0054B650. The active weapon's type, else the natural weapon type.
    int WeaponType();
    // 0054B680. Is the active weapon a bow or crossbow (types 5, 6)? 0/1 in EAX.
    BOOL HasRangedWeapon();
    // 0054B6C0. Does the unit attack from a distance: a ranged weapon, or (without a weapon) a
    // natural weapon other than 0x10 with a reach above 1? Callers test AL.
    BOOL AttacksFromRange();
    // 0054B760. The item's +0x4C (unsigned) scaled by 15 / stats +0x30 (unscaled when that is
    // 0), rounded. 0 for no item.
    int ScaledItemValue(const CItem* item);
    // 0054B9C0. For `kind` 3: the item prototype's +0x6C (or the truncated stats +0x8C without an
    // item) scaled like ScaledItemValue; 0 for other kinds.
    int ScaledAttackValue(int kind, CItem* item);
    // 0054BA30. The damage of the current attack per damage type, written to out[1..7]: (damage +
    // spread) of the weapon (party units) or of the natural attack, scaled, times each weight.
    void GetDamageByType(std::int16_t* out);
    // 0054BB20. The damage of the spell from `source` by damage type: out[0] = 0x7FFF, out[1 + its
    // type] = its truncated +0x24, the rest of the 16 bytes 0.
    void GetSpellDamage(std::uint32_t source, std::int16_t* out);
    // 0054B7C0. The quick item of `source` (high bit ignored) if it carries a spell (class 0x13,
    // category 5 or 8), else null.
    CItem* SpellCarrier(std::uint32_t source);
    // 0054B820. Is `source` a quick item of category 8 carrying a spell? Callers test AL.
    bool IsScrollSource(std::uint32_t source);
    // 0054B880. The spell (CSpell) behind `source`: a spell slot, or the spell of a carrier item.
    void* SpellOfSource(std::uint32_t source);
    // 0054BF80. Can the spell of `action` be cast now: a carrier with enough charge (category 5),
    // or a spell the party unit has the mana for? Callers test AL.
    bool CanCast(const UnitAction* order);
    // 0054B520. The target point of the first order of kind 1 (+0x190, then +0x1B4). 0/1.
    BOOL GetMovePoint(float* out);
    // 0054C070. The distance from which `action` can be carried out.
    float ActionDistance(const UnitAction* order);
    // 0054C800. False only for a melee order (kind 3 without a ranged weapon, or 6) on a living
    // unit standing on its own (+0x324 == 0) whose prototype +0x24 plus its height is 1.5 or more.
    // Callers test AL.
    bool CanReachTarget(const UnitAction* order);
    // 00550050. 1 << the weapon type (natural weapon types 7 and above: 0).
    std::uint32_t WeaponTypeMask();
    // 005500A0. Animation bits of a movement mode: 0 0x30000, 1 0x20000, 2 0x10000 (only with
    // `flag4` or for units other than heroes), 4 0x8000, else 0.
    std::uint32_t MoveModeMask(const MoveModeState* mode);
    // 00556760. Virtual slot 24: the level, log(stats +0x04 / g7C5488) / log(g7C548C), rounded.
    int Level();
    // 00553220. Virtual slot 49: sets the unit's flags_ bit 0x10000000 when it has an effect of
    // type 0x20 and a natural weapon other than 0x10, else clears it.
    void UpdateSpellBlockFlag();
    // 00555A00. Virtual slot 50: what the unit attacks with.
    void GetAttackParams(AttackParams* out);
    // 00555BD0. The attack roll of `attacker` against this unit: random spread + attack, minus a
    // distance penalty, plus a speed ratio unless `backstab`; at least 0. Draws from the global
    // ISAAC generator when the attacker's spread is positive.
    float AttackRoll(CUnitServer* attacker, bool backstab);
    // 00553DF0. Decides whether `hit` (from `attacker`, may be null) hits: sets hit +0x20 to
    // FLT_MAX (hit) or -FLT_MAX (miss) and returns 1 for a hit. Elemental hits always hit.
    BOOL ResolveHit(DamageInfo* hit, CUnit* attacker);
    // 0054F630. Builds the hit of a weapon (or natural) attack into `out`: damage weights, attack
    // roll (+0x20), damage (+0x24), location (+0x2C), backstab multiplier (+0x34) and the called
    // shot flag (+0x38). Draws from rand() and the ISAAC generator. Returns 1.
    BOOL BuildAttackHit(DamageInfo* out, int backstab, int location, char calledShot);
    // 0054F580. Builds the hit of an attack on `target` into pendingHit: a backstab when the unit
    // is behind the target (00530A60), unless the target's controller has noBackstab or the
    // weapon is ranged. Returns 1.
    BOOL PrepareAttack(CEngineObject* target, int location, char calledShot);
    // 00553F40. Virtual slot 20: the unit is hit by `hit` from `attacker` (may be null) on behalf
    // of `source` (whose player gets the experience for a kill; may be null). Resolves the hit if
    // that has not been done, logs it ("Unit 12 was attacked by 34 skill=... damage=... destHL=Body"),
    // and unless the unit's player protects it, applies the damage when the hit beats the defence.
    // Returns 1 when damage was applied; on a miss the victim turns hostile and remembers the
    // attacker.
    BOOL TakeHit(DamageInfo* hit, CUnit* attacker, CUnit* source);

    // Not rewritten (forwarders):
    // 0054F8E0. Casts the spells of carried items that trigger on being hit (or on attacking).
    void TriggerItemSpells(char onHit);
    // 00553AC0. Reacts to damage: death (when the unit is now dead) or alarm, AI notifications.
    void OnDamaged(CUnit* attacker, int hitKind);

    CUnit* unit;                    // +0x01C
    std::uint8_t unknown_020[0x108 - 0x20];
    DamageInfo pendingHit;          // +0x108 the hit of the attack in progress
    std::uint8_t unknown_144[0x168 - 0x144];
    MoveModeState moveMode;         // +0x168
    MoveModeState moveMode2;        // +0x170
    std::uint8_t unknown_178[0x190 - 0x178];
    UnitAction action;              // +0x190
    UnitAction action2;             // +0x1B4
    std::uint8_t unknown_1D8[0x1E0 - 0x1D8];
    UnitStats stats;                // +0x1E0 (0x704 bytes; its +0x6F8 dword gets bit 0x10 on a hit)
    // Further fields up to at least +0xA98 (lists of nearby units, animation state); not needed.

    // +0x174 (moveMode2.flag4) is also read by the hit code: while it is set the unit cannot be
    // backstabbed (0054F580 on the target's controller, 00553DF0 on the defender).
    std::uint8_t NoBackstab() const { return moveMode2.flag4; }
};
static_assert(offsetof(CUnitServer, unit) == 0x1C);
static_assert(offsetof(CUnitServer, pendingHit) == 0x108);
static_assert(offsetof(CUnitServer, moveMode) == 0x168);
static_assert(offsetof(CUnitServer, moveMode2) == 0x170);
static_assert(offsetof(CUnitServer, action) == 0x190);
static_assert(offsetof(CUnitServer, action2) == 0x1B4);
static_assert(offsetof(CUnitServer, stats) == 0x1E0);
