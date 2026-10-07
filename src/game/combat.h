#pragma once

// Combat: the server-side unit controller (GetClassName "CUnitServer", class id 0x3E, vtable
// 0073EF38; its abstract base has vtable 0073EDBC). Every creature on the server has one at
// CUnit + 0x240; it keeps the unit's computed statistics, its current orders, and the attack /
// hit code. Field and method names are provisional, from how the code uses them.

#include "spell.h"
#include "unit.h"

#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

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

    // 00545F70 (combat_server_slots.cpp): kind 9, no target, flag18 0; the rest untouched. Returns
    // this.
    UnitAction* Construct();
    // combat_tick.cpp. 0054B320: copies `from` (the target referenced); returns this.
    UnitAction* Assign(const UnitAction* from);
    // 0054B230: kind 9, target released, flag18 / +0x1C / +0x20 cleared.
    void Reset();
    UnitAction* CallAssign(const UnitAction* from);
    void CallReset();
    // The inlined counted-reference assignment of the target (the order setters): the old one
    // released, the new one referenced; nothing when it is the same.
    void Retarget(CEngineObject* object) {
        if (object == target) return;
        if (target) target->Release();
        target = nullptr;
        ++object->refCount_;
        target = object;
    }
};
static_assert(sizeof(UnitAction) == 0x24);

// One step of an animation list (0xC bytes; built by 0054F970 / 00550100 / 00550620). 00550A60
// constructs a blank one: animation 0xFF, +1 0xFF, +2 0, duration -1, +8 0. It never sets byte 3,
// so the original's steps carry a stack byte there; ours carry 0.
struct AnimationStep {
    std::uint8_t animation;     // +0x00 index of the unit's animation entry (0xFF: a pause)
    std::uint8_t unknown_01;    // +0x01 0xFF
    std::uint8_t unknown_02;    // +0x02 0
    std::uint8_t unknown_03;    // +0x03 not initialised by the original
    int duration;               // +0x04 game ticks
    int unknown_08;             // +0x08

    // 00550A60 (combat_server_slots.cpp): the blank step above. Returns this.
    AnimationStep* Construct();
};
static_assert(sizeof(AnimationStep) == 0xC);

// An animation list: an MFC CArray of AnimationStep (vtable 0073F01C over the base 0073EF2C;
// 00557B80 is its SetSize) and a dword after it.
struct AnimationSteps {
    void* vtable;
    AnimationStep* steps;   // +0x04
    int count;              // +0x08
    int capacity;           // +0x0C
    int growBy;             // +0x10
    int extra;              // +0x14 copied along with the steps

    // combat_step_lists.cpp (A-U5). New steps are blank (00550A60's values: 0xFF, 0xFF, 0,
    // duration -1, +8 0; byte 3 is the original's stack byte, ours 0); the list grows by
    // 00557B80 (SetSize).
    // 00544CE0 (RET 8). Appends a step of animation `animation` (low byte) lasting `duration`.
    void AddStep(int animation, int duration);
    // 00544D50 (RET 4). The last step becomes the open-ended emote `emote` (low byte into +1;
    // duration -1); a blank step is appended first when the list is empty or its last step
    // already carries an emote (+1 not 0xFF).
    void AddEmote(int emote);
    // 00544DD0 (RET 4). Every step's +8 becomes `value`.
    void SetStepFlags(int value);
    // 00544E40. The sum of the durations; -1 when one is negative (open-ended).
    int Duration() const;
    // 00544E70 (RET 4). The same steps as `other` (count, then each step's bytes 0..2, duration
    // and +8). Callers test AL.
    bool SameAs(const AnimationSteps* other) const;
    // 00544EF0 / 00544F60 (RET 4). Load: the count (into the argument's stack slot: a failed read
    // leaves the stream pointer as the count), SetSize, each step (12 bytes), extra. Save: the
    // same fields.
    void Load(UnitStream* stream);
    void Save(UnitStream* stream);
};
static_assert(sizeof(AnimationSteps) == 0x18);

// The plan of an order (0x44 bytes; CUnitServer + 0xC4 holds the current one): its animation, when
// it starts and ends, and what it aims at. 0054CA40 fills it.
struct OrderPlan {
    AnimationSteps animation;   // +0x00
    int startTime;              // +0x18 game time
    int endTime;                // +0x1C startTime + the animation's duration
    int targetId;               // +0x20 -1: none
    float rangeSq;              // +0x24 squared distance the target may move away
    std::uint8_t isCast;        // +0x28
    std::uint8_t unknown_29[3];
    int unknown_2C;             // +0x2C 1
    std::uint8_t notSkill;      // +0x30 order source != 6
    std::uint8_t unknown_31[3];
    int castKind;               // +0x34 8; for a cast: 1 (spell kind 0x28), 0 (0x29) or the
                                //       spell prototype's +0x14
    float direction[3];         // +0x38 from the unit to the target
};
static_assert(offsetof(OrderPlan, startTime) == 0x18);
static_assert(offsetof(OrderPlan, direction) == 0x38);

// 00544FD0 (combat_step_lists.cpp, A-U5). The plan emptied: the steps freed (operator delete;
// count and capacity 0), extra / targetId -1, the times 0, unknown_2C 1, rangeSq 1e38, isCast /
// notSkill 0, castKind 8.
void __fastcall OrderPlanReset(OrderPlan* plan);
static_assert(sizeof(OrderPlan) == 0x44);

class CWorldServer;   // world.h
// 0054BBC0 (combat_spell_sight.cpp; ECX world, EDX spell, RET 0x10; from 0058E850). Whether the spell
// may hit: spells whose prototype has needsSight need a line of sight of at least 0.1 from `from`
// (z = the caster's ground + BaseSpeed 00530370, a height) to the target (z = its ground + ModeSpeed
// 005302F0), or to `at` one unit above the water surface when there is no target. Returns AL.
bool __fastcall SpellHasLineOfSight(CWorldServer* world, CSpell* spell, CUnit* caster, const Vector3* from,
                                    CUnit* target, const Vector3* at);

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

// An entry of the armour spell list (CUnitServer + 0x91C, nodes of 0x14 bytes; combat_update.cpp's
// SpellElement is the same record): the item (referenced), its countdown and a "still worn" byte.
struct UnitItemSpell {
    CEngineObject* item;            // +0x00 (referenced)
    int countdown;                  // +0x04
    std::uint8_t worn;              // +0x08
    std::uint8_t unknown_09[3];
};
static_assert(sizeof(UnitItemSpell) == 0xC);
// The list instantiation's element policy: 00558EA0 zero-fills new elements (NewNode 00558B10),
// 00552B80 releases the item of a destroyed one (the pointer is left as it is).
struct UnitItemSpellElements {
    static void Construct(UnitItemSpell* elements, int count) {
        std::memset(static_cast<void*>(elements), 0, static_cast<std::size_t>(count) * sizeof(UnitItemSpell));
    }
    static void Destruct(UnitItemSpell* elements, int count) {
        for (int i = 0; i < count; ++i)
            if (elements[i].item) elements[i].item->Release();
    }
};
using UnitItemSpellList = mfc::CList<UnitItemSpell, UnitItemSpellElements>;

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
    // 0054B580. Does the current order count as fighting (for the battle music, 0066E430): an attack
    // (kind 3), or a cast (4, 5) of a spell of type 0-8 or 0x1A from a spell slot or a spell
    // carrier (quick item category 5 or 8). Rebirth's FixPlaingBattleMusicFromAntimagic drops 0x1A
    // (antimagic). Callers test AL.
    bool IsFighting();
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
    // map_object_lifetime.cpp. 00546230. The destructor body: the orders and the AI brain's
    // members destroyed, the referenced objects released, the arrays freed, then the base
    // controller (vtable 0073EDBC) and the engine object base.
    void DestructBody();
    // combat_server_slots.cpp (A-U6). 005460E0 (slot 0, RET 4): DestructBody, then operator delete
    // when `flags` bit 0 is set; returns this. 00546090 (slot 16): the motion. 005460B0 (slot 17):
    // nothing. 00552F30 (slot 53): slot 45 (UpdateMoveFactors; CUnitServer is the only class with
    // this vtable).
    CUnitServer* DeletingDestruct(int flags);
    AIPath* MotionSlot();
    void EmptySlot17();
    void UpdateMoveFactorsSlot53();
    AIPath* MotionPath() { return reinterpret_cast<AIPath*>(motion_); }
    // unit_create.cpp. 00552F40. Takes `stats` (0x704 bytes) as the unit's statistics: marks
    // the unit's part 4 changed, refills the body slots between two calls of slot 19 and sets
    // the server companion's hit points and mana to their maxima. Returns the companion (EAX).
    std::uintptr_t SetStats(const UnitStats* stats);
    // unit_create.cpp. 00552780. Remark `code` (below 0x2A, or 0x30) to the unit's player
    // (message 0x0B); nothing without a player.
    void Shout(std::uint32_t code);
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
    // byte), the point's z replaced by `z` (sic). A party unit with a real point (z not -1313)
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

    // combat_order_setters.cpp (A-U5): the other setters, as the AI, scripts and the player's
    // commands call them.
    // 0054A5A0. Would the unit fit its cell in movement mode `mode` (3 counts as 2)? True at once
    // for the current mode (+0x168); else the unit's mode (+0x264) becomes `mode` for the test (the
    // unit off the sector map, its size class updated) and is put back. Callers test AL.
    bool FitsMoveMode(int mode);
    // 00555270. The unit's wanted movement style (+0x268: 0 lie, 1 crawl, 2 walk, 3 run). When it
    // fits (FitsMoveMode), a party unit hears message 9, unless running and out of breath (stats
    // 005241C0). Other than walking and running the current order's +0x20 (run) is cleared.
    void SetMoveStyle(int style);
    // 005552E0. The weapon slot the unit wants (CUnit +0x270).
    void SetWantedWeapon(int slot);
    // 005553B0 / 00555480 / 00555570 / 00555630. A skill or use order (kind 6) on `target`:
    // stealing (source 1; a living other object), taming (2; a living unit of another player, not
    // a hero), use (0, +0x1C = `spot`; any object) and searching a body (0x18A95; a dead object
    // other than the unit). +0x20 = `run`; dropped when the planner (0054E600, shouting 6 / 5 /
    // 5 / 4) refuses.
    void OrderSteal(CEngineObject* target, int run);
    void OrderTame(CEngineObject* target, int run);
    void OrderUse(CEngineObject* target, int spot, int run);
    void OrderSearchBody(CEngineObject* target, int run);
    // 0054C760 (RET 4). Is `target` (a body to search) taken: with flags_ bit 0x20000000, when the
    // world does not share it with this unit's id (005949D0); else when a living unit of the
    // brain's nearby list (+0xF4) is searching it (kind 6, source 0x18A95) in state 5 or with
    // +0x18E set. Callers test AL.
    bool TargetBusy(CEngineObject* target);
    // 00555700. Plays the unit's animation `name` (from its figure's animation database) as the
    // queued order: kind 8 for keys with bit 31, else 7; +0x1C = the entry's index dword.
    void PlayAnimation(const char* name);
    // 00555840. Drops everything: state 6, the size class updated, the effect object removed, the
    // plan reset, the unit standing still, both orders reset and the queued one emptied (kind 0).
    void StopAction();
    // 005567A0 / 005567E0. Virtual slots 36 / 37 (CUnit::AcceptsTarget asks for the animation bits
    // 0x10 / 0x20 by them): does the unit wear an armour piece in slots 3..6 / in the first three
    // (body, trousers, helmet; CUnit +0x48C..+0x498 / +0x480..+0x488)? 0/1.
    BOOL HasOtherArmour();
    BOOL HasMainArmour();

    // combat_stat_slots.cpp (A-U5): the statistics slots.
    // 005533B0. Virtual slot 19: the vitals recomputed (with `withHitPoints` their hit points
    // first, UnitStats::RecomputeHitPoints) in the server companion's block; a pending attack
    // time is rescaled by the old / new attack rate (CombatStats +0x30), rounded; the unit's part 0
    // changed. Returns 1.
    BOOL RefreshVitals(char withHitPoints);
    // 00553450. protection[1..7] from the body (PackBodyProtection) and protection[0], the defence:
    // a party unit with a weapon its weapon skill bonus (00526C70), others their defence; times 1
    // for a party unit, else the difficulty's factor (007C54B8); truncated.
    void UpdateProtection();
    // 00553500. Virtual slot 22: the statistics repacked from the parameters (+0x330), the hit
    // flags (stats +0x6F8) cleared, the protection updated, part 4 changed; motionChanged set when
    // the movement speeds (CUnit +0x340) changed.
    void RepackStats();
    // 00553560. Virtual slot 26 (RET 4): the armour piece the unit wears in the slot of armour
    // prototype `key >> 16` (its type) added to the layers. Returns false (AL).
    bool AddArmourLayerOf(std::uint32_t key);
    // 005535C0 / 005535E0. Virtual slots 27 (RET 4, the argument unread; returns true) / 28: the
    // armour layers rebuilt / the outer layers cleared (returns UnitStats::ClearOuterLayers's 0).
    bool RebuildArmourLayers(int unused);
    int ClearOuterArmourLayers();
    // 005535F0. Virtual slot 29: the armour spells and the AI's attack options (005CEED0) rebuilt.
    // Returns 1.
    BOOL RefreshEquipment();
    // 00552CD0. Virtual slot 38: the claims dropped (claimedHostile) and the calls heard with a
    // hostility mask (brain +0x178, 005CCBF0).
    void ForgetClaims();
    // 00553360 (RET 4). Copies the statistics (0x704 bytes) to `out`.
    void CopyStats(UnitStats* out);
    // 00553380 (RET 4). Experience gained (UnitStats::GainExperience), then RefreshVitals(0)
    // (slot 19; only CUnitServer's vtable has it), whose 1 it returns.
    BOOL AddExperience(float amount);

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

    // combat_save.cpp (third round).
    // 00555D10. Virtual slot 9 (Load): the controller's state (animation lists, the motion and
    // plan objects, both orders, the stats copy, movement factors, the brain, the voice, the
    // effect object and the attackers list), version dependent from 0x44 on.
    void LoadServer(UnitStream* stream);
    // 005561C0. Virtual slot 10 (Save): the same in the same order, at the current version.
    void SaveServer(UnitStream* stream);

    // combat_sight.cpp (third round): can an order be carried out from here, and when.
    // 0054BCB0. What a cast (kinds 4, 5) or an order on a unit must see: false for a point order
    // with flag18, a missing spell, a spell whose prototype +0x40 is 0, or (other kinds) a target
    // that is not a unit. Callers test AL.
    bool FindSightTarget(const UnitAction* order, struct SightTarget* out);
    // 0054C2F0. Can the unit carry out `order` at `time` with its target at `point`: in range for
    // AI units, no living unit in the way of a melee or skill order, and a line of sight when the
    // order needs one. Callers test AL.
    bool CanActFrom(const UnitAction* order, const float* point, int time);
    // 0054CA40. Plans `order`: builds its animation (0054F970 / 00550100 / 00550620, which draw
    // random numbers), then looks for the first moment at which the target is in reach, the unit
    // can turn to it in time, and CanActFrom holds; fills `plan` and returns true then.
    bool PlanAction(const UnitAction* order, struct OrderPlan* plan);
    // 0054C6B0. PlanAction into a scratch plan, with the target unit's flags_ bit 0x8000000 set
    // meanwhile. Callers test AL.
    bool CanPlanAction(const UnitAction* order);
    // The same through the game's addresses (for the verify build's wrappers).
    bool CallCanActFrom(const UnitAction* order, const float* point, int time);
    bool CallPlanAction(const UnitAction* order, struct OrderPlan* plan);
    bool CallFindSightTarget(const UnitAction* order, struct SightTarget* out);

    // combat_steps.cpp (third round): the animation steps of an order. The unit's animation
    // entries are looked up by a key of weapon, movement and action bits (CUnit 005301B0).
    // 00551A40 / 00551A90. The duration / the moment of the action of animation entry `index`, in
    // ticks: the entry's +0x20 / +0x40 divided by the unit's speed factor for it (005327D0),
    // truncated; 0 without the entry.
    int AnimationDuration(std::uint32_t index);
    int AnimationMoment(std::uint32_t index);
    // 005516D0. The step that changes the movement mode `from` into `to` (only heroes, or a change
    // involving mode 4); false when there is none.
    bool BuildTransition(struct AnimationSteps* steps, const MoveModeState* from, const MoveModeState* to);
    // 00550100. The steps of an order of `kind` with `source` (for kind 3 `source` is the attack key
    // from BuildAttackSteps): the animation, framed by stepping out of and back into the movement
    // mode for skills; *duration = the ticks until the action happens.
    void BuildOrderSteps(struct AnimationSteps* steps, int* duration, int kind, std::uint32_t source);
    // 00550620. The steps of a cast taking `value` ticks; draws a random number for heroes.
    void BuildCastSteps(struct AnimationSteps* steps, int* duration, int kind, int value);
    // 0054F970. The steps of an attack on `target`: the hit location (plan +0x2C) and the height of
    // the blow from both units' heights and the distance. Draws random numbers. Returns 1.
    bool BuildAttackSteps(struct OrderPlan* plan, struct AnimationSteps* steps, int* duration, CEngineObject* target);
    // The same through the game's addresses.
    bool CallBuildAttackSteps(struct OrderPlan* plan, struct AnimationSteps* steps, int* duration, CEngineObject* target);
    void CallBuildOrderSteps(struct AnimationSteps* steps, int* duration, int kind, std::uint32_t source);
    void CallBuildCastSteps(struct AnimationSteps* steps, int* duration, int kind, int value);
    bool CallBuildTransition(struct AnimationSteps* steps, const MoveModeState* from, const MoveModeState* to);
    int CallAnimationDuration(std::uint32_t index);
    int CallAnimationMoment(std::uint32_t index);

    // combat_tick.cpp (third round): the per-tick order machine.
    // 0054B390. Every tick: takes over the queued order, else follows the path and continues the
    // current order (ContinueAction for actions; a finished move or animation lets the queue on).
    void UpdateOrders();
    // 0054A900. Takes over the queued order (+0x1B4) by its kind; true when it started.
    bool StartQueuedOrder();
    // The move part of StartQueuedOrder (kind 1): plans a path with the units near the goal lifted
    // off the sector map; done orders let the next one start.
    bool StartMove();
    // 0054DD70. Continues an action order while its target is valid and seen, else ends it
    // (party messages 0x0B / 0x0E / 0x12) and calls 00549690.
    void ContinueAction();
    // 0054D960. Plans the action (PlanAction); when it can start, rolls an attack's hit and starts
    // the action (005494D0), else closes in (ApproachTarget) or drops the order.
    void ExecuteOrder();
    // 0054D140. Closes in on the order's target or point: waits when in reach, else plans a path
    // to where the target will be.
    void ApproachTarget();
    // 0054A620. The movement records (+0x168 / +0x170) from the unit's modes and the order.
    void SyncMoveModes();
    // 0054A6E0. Takes over the queued order (an inlined copy of StartQueuedOrder's common start).
    void TakeQueuedOrder();
    // 00546830. The unit's second movement mode, marking it changed.
    void SetUnitMoveMode2(int mode);
    bool CallStartQueuedOrder();
    void CallSyncMoveModes();
    void CallExecuteOrder();
    void CallApproachTarget();
    void CallContinueAction();
    void CallTakeQueuedOrder();
    void CallSetUnitMoveMode2(int mode);
    bool CallCanPlanAction(const UnitAction* order);

    // combat_update.cpp (fourth round): the update and the animation queue.
    // 00552300. Virtual slot 6, once per world update. A living unit regenerates, tells the AI
    // events where it is, ticks its item spells, party units their memory and boredom, then the
    // alarm, the brain and hostility; the orders (when not frozen, reacting to a hit or acting),
    // the state and the animation follow. A dead one sinks to the ground and, once its death
    // animation is over, reports its death and drops its orders. Returns 1.
    int UpdateServer();
    // 005499F0. The state machine of +0x164 (0 idle / 1 moving: the movement tick; 2 / 4: a hit
    // reaction; 3: the rest of an action's animation, then 6; 5: TickAction), after ending an
    // acting order whose target went out of reach and restoring the queue a hit reaction saved.
    // hitDirection is -1 afterwards.
    void UpdateState();
    // 00546F30. Moves the animation queue on to the world's time and shows its current step on the
    // unit (+0x254..+0x260); frozen or reacting units keep their times shifted.
    void AdvanceAnimation();
    // 00546B40. Drops the queue's steps that are over (a pause after one takes its animation over);
    // true when one was dropped.
    bool PopFinishedSteps();
    // 00546C10. Shows the queue's first step on the unit (not a pause).
    void ShowCurrentStep();
    // 00546C40. The queue becomes `steps` (copied in twice) and starts.
    void PlaySteps(const AnimationSteps* steps);
    // 00547090. Sets the state; other than 5 (acting) the size class is updated and the effect
    // object dropped, 5 sets +0x180 to 10 updates from now.
    void SetState(int newState);
    // 005467B0. The unit's standing flag (CUnit + 0x278), re-registered on the sector map.
    void SetStanding(int standing);
    // 00547280. The motion stops: the unit stays where it is, facing its direction; standing 0.
    void StandStill();
    // 00547100. Plays the reaction to a hit from hitDirection (BuildDeathAnimation without dying),
    // saving the queue (+0x148), unless the unit is idle or the reaction would delay the next attack.
    void PlayHitReaction();
    // 00549690. Ends the current order: aborted (or not started, or with an open-ended animation)
    // the state becomes 6, else 3 with what is left of a three-step plan played; the plan is reset
    // and the unit stands still.
    void EndOrder(char abort);
    // 00549160. State 5: the planned action starts at its time (animation, a cast's effect), the
    // unit follows its motion meanwhile, and at the end the action is finished (FinishAction).
    void TickAction();
    // 00549D40. Can the acting order still be carried out: casting allowed, the target (or a
    // spell's point) not dead and within the plan's range at its end time. Callers test AL.
    bool ActionInReach();
    // 00552B90. The armour's periodic spells (list +0x920): each casts on the unit when its
    // countdown runs out and the item's charge covers it (type 0x18: one a tick, when hurt).
    void TickItemSpells();

    // combat_motion.cpp (fourth round): movement modes, idle and movement animations, paths.
    // 00549D00. False while an attack turns (state 2) and `ticks` from now would pass the next
    // attack's time (+0x184). Callers test AL.
    bool CanReactFor(int ticks);
    // 00548DE0 / 00548FA0. Play `steps` (taken by value and destroyed) as an animation order
    // (state 3) / an emote (state 4): the queue takes it, the unit stops facing its direction.
    void PlayAnimationOrder(AnimationSteps steps);
    void PlayEmoteOrder(AnimationSteps steps);
    // 00550D90. The idle animation into `steps`: the emote 0xFE for a hero walking (mode 2 without
    // flag4), else the idle entry of the weapon and movement key (| 0x80580000), if any.
    void BuildIdle(AnimationSteps* steps);
    // 00550F30. The idle animation (+0x960), rebuilt when the key (+0x95C) changed.
    AnimationSteps* IdleAnimation();
    // 005474B0. Unless idle, moving or turning: the idle animation, rebuilt when needed, plays.
    void PlayIdle();
    // 005511A0. The movement animation of the current mode into `steps`: looped (an emote of the
    // entry; a hero's by stamina +0x238 against 007C5448 / 007C5440) or `once` (the entry's
    // duration); every step's +8 is 2.
    void BuildMoveSteps(AnimationSteps* steps, char once);
    // 00551400. The movement animation (+0x944, key +0x940): for a hero the start once, then the loop.
    AnimationSteps* NextAnimation();
    // 005494D0. Starts the planned action: its steps get `stepFlags` in byte 2, the idle animation
    // plays unless idle, moving or turning, state 5, and the motion turns to the plan's direction
    // or follows its target (until its end when `follow`); not standing.
    void StartAction(char follow, char stepFlags);
    // 00548520. The first movement record's mode 3 (+0x16D) while the second asks for it (+0x175):
    // taken on a cheap cell when not out of breath (and, without a player's permission, with
    // stamina); dropped after 5 updates otherwise; out of breath drops the second's too (remark).
    void UpdateSneak();
    // 0054A150. Takes the second movement record (+0x170) as the first: back with message 0x0D
    // when the unit no longer fits its cell; an idle unit changing mode stops facing its way; a
    // transition animation plays (not while attacking); an empty queued order becomes the current
    // one. True when the mode changed.
    bool ChangeMoveMode();
    // 00548700. Plans the motion to `point` within `range` (FindPath): no path stands the unit still
    // (state 6, +0x145 from the path's +0xA9); a path makes it idle (state 0) with its movement
    // animation going on. Callers test AL.
    bool PathTo(const float* point, float range);
    // 005468D0. Fills the CList `list` with the units lifted off the sector map for a path: away
    // from `point`, those near it outside the player's group; the party members that move (with
    // `moving`) or follow this unit. Only for a player's units with +0x100 == 1.
    void LiftNearby(void* list, const float* point, char moving);
    // 0054EB30. Tells a party unit's player how a path to `path`'s goal turned out (0066D490 and
    // messages 0x0E / 0x01), unless the brain's mode is 6.
    void ReportPath(void* path, char partial);
    // 00546480. The controller's reset (constructor and Load): orders, queue, plan, hit, records,
    // effect object, brain, alarm, factors, stats, animation caches, the armour spell list.
    void ResetController();
    // 00554450. Drops the current order (and the queued one unless an animation or emote), takes
    // the movement records from the unit's modes, stops facing its way (state 6), takes up the
    // wanted weapon and, unless turning, plays the idle animation (a random phase on an empty
    // queue) and turns (state 2).
    void ResyncWithUnit();
    // combat_taming.cpp. 00551AE0. A tame attempt by `tamer` with `roll`: above 10 x slot 39 plus
    // the taming skill (stats value 2) it moves the unit a stage along (+0x908: 1 not hostile to
    // the tamer's side, 2 on that side, 3 in the tamer's party with a tamed mode); a unit with a
    // player or a failed roll is attacked instead (taming by that side undone). 0/1.
    BOOL TryTame(CUnit* tamer, float roll);
    // 00552280. Undoes the taming: the tamed mode (8) removed, no player, all relations, the old
    // side back (from stage 2), stage 0.
    void UndoTaming();
    // combat_update.cpp. 005528F0. Rebuilds the armour spell list (+0x91C) from the items the
    // unit wears (weapons only the one in use) whose spell is periodic (+0x3C bit 0x20000): kept
    // entries keep their countdown, new ones start at 0, entries of items no longer worn go.
    void RefreshItemSpells();

    // combat_planning.cpp (fourth round): the way to an order's target.
    // 0054DF50. For a use order on a target (kind 6, source 0): the reachable spot the target
    // offers (its slot 44, for the unit's size) whose path is cheapest becomes the order's point
    // (flag18 set), its path into `motionOrder` (if given); false when no spot can be reached.
    // Every order's point gets z -1 first. Callers test AL.
    bool FindUseSpot(UnitAction* order, void* motionOrder);
    // 0054E250. Simulates the unit along `motionOrder` (a path order, kind 2) in steps of 5 ticks
    // until the order's target is in reach (and seen, when the order needs a line of sight); the
    // time it takes (ST0), 0 for other kinds.
    double TimeUntilAction(const UnitAction* order, void* motionOrder);
    // 0054E600. Plans the way to `order`'s target: the use spot, the path (target lifted off the
    // sector map) and whether the unit can act from its end, with the player's path message;
    // the unit shouts `shout` when it can, 0x0B / 0x0E when not. Units without a player plan only
    // use orders. Callers test AL.
    bool PlanOrderRoute(UnitAction* order, std::uint32_t shout);
    // 00554830. A party unit's move to `point`: the units in the way lifted (LiftNearby), a runner
    // planning as walking where it can, FindPath within `range` (the order's z bits), the units put
    // back and the result reported (ReportPath).
    void PlanMove(const float* point, int moving, int run, float range);

    // The same through the game's addresses (combat_orders.cpp):
    // 00554830 (PlanMove).
    void TurnTo(const float* point, int walk, int run, std::uint32_t z);
    // 0054E600 (PlanOrderRoute).
    bool PlanOrder(UnitAction* order, int arg);

    // combat_object_slots.cpp (A-U5).
    // 00552D00 (RET 8). `claimant` claims the players in `mask` are enemies of this unit: they join
    // claimedHostile and the AI memory hears a call of kind 0x1E carrying the mask, with the text
    // "Unit <claimant id> claims that player <n> are enemies! " for each player n in the mask.
    void ClaimHostility(CUnit* claimant, std::uint32_t mask);
    // 00553610. Virtual slot 25 (RET 4): takes up the weapon in `slot` (-1, none, when the slot is
    // empty or the weapon's condition rounds to 0 or less) as the active and wanted weapon (part 6
    // changed); a party unit's AI options are rebuilt, the armour spells refreshed. 1 when the
    // weapon was taken up.
    BOOL TakeUpWeapon(int slot);
    // 00552670. Virtual slot 42 (RET 4): `object` is being deleted: orders aimed at it end (the
    // current one reset, then Idle when the queued one is empty; a queued one is emptied unless an
    // animation), a plan aimed at it ends (EndOrder(0)), the brain forgets it (005D58D0) and the
    // leader followed is dropped when it is that object.
    void OnObjectDeleted(CEngineObject* object);
    // 00553260. Virtual slot 44: a living unit is set up again where it stands: off the sector
    // map, its figure (005179E0), size classes and size class; when it cannot leave its cell it
    // moves to a free cell near it (005BA9D0), or with none it takes a default hit (TakeHit; a
    // unit that cannot stand dies); then back on the sector map.
    void Resettle();
    // 00546450. Virtual slot 2 (Create): ResetController, then the brain takes this controller as
    // its owner (005D5080). Returns 1.
    int CreateServer();
    // 00546080. Virtual slot 5: ResetController (a tail jump).
    void ResetSlot();
    // 005460A0. Virtual slot 39: the taming stage.
    int TameStage();

    // Not rewritten (forwarders):
    // 00550A80 (combat_steps.cpp). The death (or fall) animation for a blow from `direction`
    // (HitDirection's bits): `dying` adds the death bits, `fall` (lying down) stops the steps with
    // +8 = 2 and may pick a side at random. Only the low bytes of `dying` and `fall` count.
    void BuildDeathAnimation(struct AnimationSteps* steps, std::uint32_t direction, int dying, int fall);
    void CallBuildDeathAnimation(void* steps, std::uint32_t direction, int dying, int fall);

    CUnit* unit;                    // +0x01C
    std::uint8_t unknown_020;       // +0x020 cleared on death
    std::uint8_t unknown_021;       // +0x021 cleared on death
    std::uint8_t unknown_022[2];
    AnimationSteps queue;           // +0x024 the animation queue (its extra: the time it started, -1 unset)
    std::uint8_t motion_[0xC4 - 0x3C];       // +0x3C the motion (AIPath, ai_map.h): MotionPath()
    OrderPlan currentPlan;          // +0x0C4 the plan of the current order (0054CA40 fills it)
    DamageInfo pendingHit;          // +0x108 the hit of the attack in progress
    std::uint8_t keepHitDirection;  // +0x144 while set, OnDamaged leaves hitDirection alone
    std::uint8_t stoodBlocked;      // +0x145 the unit stood still because its path search failed
                                    //        (the path's `failed`, +0xA9, when it stopped)
    std::uint8_t motionChanged;     // +0x146 set when the movement speeds or mode changed (cleared by PathTo)
    std::uint8_t movingAnimation;   // +0x147 the movement animation is in the queue (cleared when no
                                    //        path is found)
    AnimationSteps savedQueue;      // +0x148 the queue a hit reaction saved (00547100)
    std::uint32_t hitDirection;     // +0x160 HitDirection of the last hit that did not kill
    int state;                      // +0x164 0 idle, 5 casting, ...
    MoveModeState moveMode;         // +0x168
    MoveModeState moveMode2;        // +0x170
    int sneakWait;                  // +0x178 updates mode 3 was kept without a cheap cell (UpdateSneak)
    CEngineObject* effectObject;    // +0x17C (referenced) removed from the world on death
    int actingCheckTime;            // +0x180 world tick + 10 when acting starts (StartAction), -1 reset
    int nextAttackTime;             // +0x184 world tick of the next attack
    int unknown_188;                // +0x188 saved and loaded
    std::uint8_t unknown_18C;       // +0x18C saved and loaded
    std::uint8_t unknown_18D;       // +0x18D
    std::uint8_t unknown_18E;       // +0x18E TargetBusy counts a search under way when set (or in state 5)
    std::uint8_t unknown_18F;
    UnitAction action;              // +0x190
    UnitAction action2;             // +0x1B4
    std::uint8_t followsLeader;     // +0x1D8 the chase follows `followedLeader`'s target (combat_tick.cpp)
    std::uint8_t unknown_1D9[3];
    CEngineObject* followedLeader;  // +0x1DC (referenced) the party unit whose attack this one joins
    UnitStats stats;                // +0x1E0 (0x704 bytes; its +0x6F8 dword gets bit 0x10 on a hit)
    std::int16_t protection[8];     // +0x8E4 [0] the defence value (00553450), [1..7] the body's protection
                                    //        per damage type (UnitStats::PackBodyProtection)
    float unknown_8F4;              // +0x8F4
    float moveFactors[4];           // +0x8F8
    int tameStage;                  // +0x908 0..3 (TryTame)
    int ownSide;                    // +0x90C the unit's own side while tamed (TryTame)
    int tamerSide;                  // +0x910 the tamer's side (TryTame)
    int speedClass;                 // +0x914
    void* unknown_918;              // +0x918 cleared by the reset and Load
    UnitItemSpellList itemSpells;   // +0x91C the armour spell list (RefreshItemSpells)
    int unknown_938;                // +0x938 cleared when an order starts and by the move style /
                                    //        wanted weapon setters
    int waitFrame;                  // +0x93C a frame number; the movement tick waits for a party member
                                    //        it blocks while the frame is below this + 2
    std::uint32_t moveKey;          // +0x940 the key moveSteps were built for
    AnimationSteps moveSteps;       // +0x944 the movement animation (NextAnimation)
    std::uint32_t idleKey;          // +0x95C the key idleSteps were built for
    AnimationSteps idleSteps;       // +0x960 the idle animation (IdleAnimation; its extra +0x974: start time)
    int unknown_978;                // +0x978 cleared by the reset
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
static_assert(offsetof(CUnitServer, currentPlan) == 0xC4);
static_assert(offsetof(CUnitServer, unknown_938) == 0x938);
static_assert(offsetof(CUnitServer, motionChanged) == 0x146);
static_assert(offsetof(CUnitServer, nextAttackTime) == 0x184);
static_assert(offsetof(CUnitServer, unknown_18E) == 0x18E);
static_assert(offsetof(CUnitServer, tameStage) == 0x908);
static_assert(offsetof(CUnitServer, protection) == 0x8E4);
static_assert(offsetof(CUnitServer, followedLeader) == 0x1DC);
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
static_assert(offsetof(CUnitServer, queue) == 0x24);
static_assert(offsetof(CUnitServer, stoodBlocked) == 0x145);
static_assert(offsetof(CUnitServer, movingAnimation) == 0x147);
static_assert(offsetof(CUnitServer, savedQueue) == 0x148);
static_assert(offsetof(CUnitServer, sneakWait) == 0x178);
static_assert(offsetof(CUnitServer, actingCheckTime) == 0x180);
static_assert(offsetof(CUnitServer, unknown_188) == 0x188);
static_assert(offsetof(CUnitServer, unknown_18C) == 0x18C);
static_assert(offsetof(CUnitServer, followsLeader) == 0x1D8);
static_assert(offsetof(CUnitServer, unknown_8F4) == 0x8F4);
static_assert(offsetof(CUnitServer, ownSide) == 0x90C);
static_assert(offsetof(CUnitServer, tamerSide) == 0x910);
static_assert(offsetof(CUnitServer, unknown_918) == 0x918);
static_assert(offsetof(CUnitServer, itemSpells) == 0x91C);
static_assert(offsetof(CUnitServer, waitFrame) == 0x93C);
static_assert(offsetof(CUnitServer, moveKey) == 0x940);
static_assert(offsetof(CUnitServer, moveSteps) == 0x944);
static_assert(offsetof(CUnitServer, idleKey) == 0x95C);
static_assert(offsetof(CUnitServer, idleSteps) == 0x960);
static_assert(offsetof(CUnitServer, unknown_978) == 0x978);

// 005460C0 / 005460D0 (slots 11 / 12; combat_server_slots.cpp): "CUnitServer" (0078FCC4) and 0x3E.
const char* __fastcall UnitServerClassName(CUnitServer* self);
int __fastcall UnitServerClassId(CUnitServer* self);
