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
    std::uint8_t unknown_34[0x60 - 0x34];
    int unknown_60;             // +0x60 2: no size classes (00552FB0)
    std::uint8_t unknown_64[4];
    std::int16_t sizeClass;     // +0x68 1..4 by size, 5/6 when loaded above 0.85, 7 above 1.5
    std::int16_t unknown_6A;
    std::int16_t sizeClass20;   // +0x6C the same for 0.2 times the size
    std::int16_t sizeClass60;   // +0x6E and for 0.6 times the size
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
    std::uint8_t unknown_B0[0xBC - 0xB0];
    float unknown_BC;           // +0xBC scales the world's +0x84 factor in the movement factors
    std::uint8_t unknown_C0[0xFC - 0xC0];
    float noise;                // +0xFC how loud the unit's attacks are (scaled by 2 for a hit,
                                //       1.5 for a skill use, passed to 005C98C0 with kind 0x1A)
    std::uint8_t unknown_100[0x110 - 0x100];
};
static_assert(offsetof(CombatStats, noise) == 0xFC);
static_assert(offsetof(CombatStats, sizeClass) == 0x68);
static_assert(offsetof(CombatStats, reach) == 0x70);
static_assert(offsetof(CombatStats, unknown_BC) == 0xBC);
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
    // 00552FB0. Virtual slot 23: the three size classes (CombatStats +0x68/+0x6C/+0x6E) from the
    // unit's load (005304A0) and size (00530560 times the model's +0x120; heroes capped at 1.89).
    void UpdateSizeClasses();
    // 005536B0. Virtual slot 45: the movement factors (+0x8F8..+0x904) from the movement mode,
    // the world's +0x84/+0x88 factors and the spell being cast, and the speed class (+0x914: 0, 1,
    // 3, 7 or 15 by the unit's speed when idle, else 15).
    void UpdateMoveFactors();

    // combat_actions.cpp: the end of an action, missiles, item spells, death, healing, skills.
    // 00549EC0. Called when the animation of the current order reaches its moment: an attack
    // (kind 3) delivers pendingHit (and when the target's body part `location` can no longer be
    // aimed at, slot kind not 3, an order aimed at it aims at "all", 6), a cast (4, 5) finishes, a skill or use (6) is applied, a quick item
    // of category 2 (10) is used up. All but attacks then clear the order. The second argument is
    // not read.
    void FinishAction(int location, char unused);
    // 0054F450. Delivers `hit` to `target`: a missile (world object 0x5B carrying the hit) for a
    // bow or crossbow, else the target's hit slot (CUnit slot 31 -> TakeHit) directly; then the
    // weapon's spell, and noise when the hit was not a backstab.
    void DeliverAttack(CEngineObject* target, DamageInfo* hit);
    // 0054F850. Casts the active weapon's spell (flag 0x10000) at `target` when its charge covers
    // the cost.
    void TriggerWeaponSpell(CEngineObject* target);
    // 0054F8E0. Casts the spells of the armour pieces that trigger on being hit (`onHit`; spell
    // prototype 0x18 triggers when not hit) and have the charge for it.
    void TriggerItemSpells(char onHit);
    // 00552C60. Takes `cost` from the item's charge and updates the party's copy of the item.
    void UseItemCharge(CItem* item, float cost);
    // 005543B0. Virtual slot 21: heals `amount` (UnitStats 00525900), recomputes and logs
    // "Unit 12 was healed". Returns 1.
    BOOL Heal(float amount);
    // 00553AC0. Reacts to damage. Not dead: the armour spells, diplomacy, the AI memory, hostility
    // claims and the hit direction. Dead: remarks, witnesses, the death state, "Unit 12 damaged to
    // death", the death animation, the AI notification and the party's reaction. Units of types
    // outside 0x32..0x36 only get flags_ bit 1.
    void OnDamaged(CUnit* attacker, int hitKind);
    // 005538D0. For each unit in `witnesses` (a CList of units) allied with this unit's side that
    // saw `victim` (is in its controller's +0xA90 list): adds `murderer` to that list (logging
    // "Unit 12 has seen murder and is forced to see unit 34") and makes the murderer hostile.
    void WitnessMurder(void* witnesses, CUnit* victim, CUnit* murderer);
    // 00552E00. When the unit's flags_ have 0x80000: clears it, adds its hostile mask to its
    // claims (+0x97C) and swaps hostility claims (00552D00) with the allied units nearby.
    void PropagateHostility();
    // 005518F0. Where `attacker` stands relative to the unit: 0x800000 in front, 0x400000 behind,
    // 0xC00000 / 0x1000000 to the sides; 0 without an attacker or with a non-zero `hitKind`.
    std::uint32_t HitDirection(CUnit* attacker, int hitKind);
    // 0054F000. Applies the skill of the current order to `target`: kind 6 with source 0x18A95
    // (a talk / trade order: 005942D0 and 005935C0), else science (0) and stealing (1), logged
    // "Unit12 is using skill science his_skill = ... target_skill = ...".
    void UseSkill(CEngineObject* target);
    // 00555180. Drops the queued order (+0x1B4) unless it is of kind 7 or 8 or there is no current
    // order; the queued order ends up of kind 0 (sic).
    void Idle();
    // 00546720. Sets the unit's dead flag (+0x274) and re-registers it with the world's +0x294
    // map; when dying, also removes and releases the effect object at +0x17C.
    void SetDead(int dead);

    // 0054EE10. Finishes the cast of the current order (kinds 4 and 5): a carrier pays from its
    // charge (not enough: nothing is cast), a party unit's spell from its mana (clamped to
    // 0..maximum); the spell goes at the target (its aim point) or the order's point, with noise
    // unless the spell's prototype +0x10 is 1. The order's source becomes -1.
    void FinishCast();
    // Order setters: each replaces the queued order (+0x1B4), as the AI and the player's commands
    // ask for them (unit_ai.h names them from the caller's side).
    // 005546B0. Kind 2 (turn) towards `direction`; +0x1C = `time`.
    void OrderTurn(const float* direction, std::uint32_t time);
    // 00554740. Kind 1 (move) to `point`: source = `walk` != 0 (low byte), +0x20 = `run` (low
    // byte), the point's z replaced by `z` (sic). A party unit with a real point (z not -1312)
    // either has +0x1C set to 1 (the world's +0x6C byte clear) or turns there (00554830).
    void OrderMove(const float* point, int run, int walk, std::uint32_t z);
    // 00554B30. Kind 3 (attack) on `target` (not null, not the unit itself) at body part
    // `location` (6, all, unless the target's part kind is 3); +0x20 = `calledShot`. Dropped when
    // the target is out of reach (message 0x0C to the party) or the planner (0054E600) refuses.
    void OrderAttack(CEngineObject* target, int location, int calledShot, int planArg);
    // 00554D00. Kind 4 (cast at a point). A scroll (quick item of category 8) is cast at once.
    // A party unit needs the spell's skill (+0x50) for a spell of its own; message 0x13 if not,
    // or while flags_ has 0x10000000.
    void OrderCastAt(std::uint32_t source, const float* point, int run);
    // 00554F50. Kind 5 (cast at `target`), as OrderCastAt.
    void OrderCast(std::uint32_t source, CEngineObject* target, int run);
    // 00555200. Unless the current order is of kind 11: the queued order becomes kind 11 (stop).
    void StopQueued();

    // The same functions called through the game's addresses (EI_ORIGINAL), so that the verify
    // build's wrappers see the calls between the rewrites.
    void CallDeliverAttack(CEngineObject* target, DamageInfo* hit);
    void CallTriggerWeaponSpell(CEngineObject* target);
    void CallTriggerItemSpells(char onHit);
    void CallUseItemCharge(CItem* item, float cost);
    void CallOnDamaged(CUnit* attacker, int hitKind);
    void CallWitnessMurder(void* witnesses, CUnit* victim, CUnit* murderer);
    void CallPropagateHostility();
    std::uint32_t CallHitDirection(CUnit* attacker, int hitKind);
    void CallUseSkill(CEngineObject* target);
    void CallIdle();
    void CallSetDead(int dead);
    // The queued order's reset that Idle and FinishAction share: kind 9 and cleared, then kind 0.
    void DropQueuedOrder();

    void CallFinishCast();

    // Not rewritten (forwarders):
    // 00554830. Turns towards / plans the way to `point` for a party unit's move order.
    void TurnTo(const float* point, int walk, int run, std::uint32_t z);
    // 0054E600. Plans the order `order` (paths, reach); false when it cannot be carried out.
    bool PlanOrder(UnitAction* order, int arg);
    // 00552D00. `claimant` claims the players in `mask` are enemies of this unit.
    void ClaimHostility(CUnit* claimant, std::uint32_t mask);
    // 00550A80. Fills `animation` (a CArray of 0xC-byte entries) for death in `direction`.
    void BuildDeathAnimation(void* animation, std::uint32_t direction, int a, int b);

    CUnit* unit;                    // +0x01C
    std::uint8_t unknown_020;       // +0x020 cleared on death
    std::uint8_t unknown_021;       // +0x021 cleared on death
    std::uint8_t unknown_022[0x108 - 0x22];
    DamageInfo pendingHit;          // +0x108 the hit of the attack in progress
    std::uint8_t keepHitDirection;  // +0x144 while set, OnDamaged leaves hitDirection alone
    std::uint8_t unknown_145[0x160 - 0x145];
    std::uint32_t hitDirection;     // +0x160 HitDirection of the last hit that did not kill
    int state;                      // +0x164 0 idle, 5 casting, ...
    MoveModeState moveMode;         // +0x168
    MoveModeState moveMode2;        // +0x170
    std::uint8_t unknown_178[0x17C - 0x178];
    CEngineObject* effectObject;    // +0x17C (referenced) removed from the world on death
    std::uint8_t unknown_180[0x190 - 0x180];
    UnitAction action;              // +0x190
    UnitAction action2;             // +0x1B4
    std::uint8_t unknown_1D8[0x1E0 - 0x1D8];
    UnitStats stats;                // +0x1E0 (0x704 bytes; its +0x6F8 dword gets bit 0x10 on a hit)
    std::uint8_t unknown_8E4[0x8F8 - 0x8E4];
    float moveFactors[4];           // +0x8F8
    std::uint8_t unknown_908[0x914 - 0x908];
    int speedClass;                 // +0x914
    std::uint8_t unknown_918[0x97C - 0x918];
    std::uint32_t claimedHostile;   // +0x97C players the unit has claimed as enemies
    // +0x980 the AI brain (AIBrain in unit_ai.h, 0x190 bytes); combat_actions.cpp reads its
    // +0x38 (claim radius), +0xD0 (perception memory), +0xF4 (CList of nearby units, head at
    // +0xF8) and +0x110 (CList of units the unit watches).
    std::uint8_t brain[0x190];
    // +0xB10 the voice (005AEA60 Say). Further fields not needed.

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
static_assert(offsetof(CUnitServer, state) == 0x164);
static_assert(offsetof(CUnitServer, moveFactors) == 0x8F8);
static_assert(offsetof(CUnitServer, speedClass) == 0x914);
static_assert(offsetof(CUnitServer, unknown_020) == 0x20);
static_assert(offsetof(CUnitServer, keepHitDirection) == 0x144);
static_assert(offsetof(CUnitServer, hitDirection) == 0x160);
static_assert(offsetof(CUnitServer, effectObject) == 0x17C);
static_assert(offsetof(CUnitServer, claimedHostile) == 0x97C);
static_assert(offsetof(CUnitServer, brain) == 0x980);
