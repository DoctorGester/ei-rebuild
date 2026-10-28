#pragma once

// Combat: the server-side unit controller (CUnitServer, class id 0x3E). Every creature on the server has one;
// it keeps the unit's computed statistics, its current orders, and the attack / hit code.

#include "ai_map.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"

#include <cstddef>
#include <cstdint>
#include <list>

constexpr int kClassUnitServer = 0x3E;

// An order of the unit: an AIOrder and two more fields. The order checks (CanReachAndPlan and the planning
// under it) take the AIOrder part only: the AI builds just that on the stack.
struct UnitAction : AIOrder {
    int param;                  // by kind: a skill's spot (6), a wait's time, an emote's animation,
                                // the studied item's id
    std::uint8_t modifier;      // run (move orders) or a called shot (attacks)

// The target is referenced.
    UnitAction* Assign(const UnitAction* from);
// Kind 9, the target released, pointIsUseSpot, param and modifier cleared.
    void Reset();
    void Retarget(CMapObject* object) {
        if (object == target) return;
        if (target) target->Release();
        target = nullptr;
        ++object->refCount_;
        target = object;
    }
};

// One step of an animation list: animation is the index of the unit's animation entry (0xFF: a pause), variant
// the emote / animation variant (0xFF: none), flags UnitSetAnimation's flags, a padding byte, duration in game
// ticks, kind (1 holds the queue time, 2 lying, a fall; CUnit::SetMotionMode). A blank step is 0xFF, 0xFF, 0,
// duration -1, kind 0. The padding byte is never set: the original's steps carried a stack byte there, ours
// carry 0, and so does the original in harness runs (harness_save_leftovers.cpp).
struct AnimationStep {
    std::uint8_t animation = 0xFF;
    std::uint8_t variant = 0xFF;
    std::uint8_t flags = 0;
    std::uint8_t padding = 0;
    int duration = -1;
    int kind = 0;
};
static_assert(sizeof(AnimationStep) == 0xC);   // saved as its 12 bytes

// A step list and a dword copied along with it (extra: the queue's start time, ...).
struct AnimationSteps : std::vector<AnimationStep> {
    // Appends a blank step of `animation` (low byte) lasting `duration`.
    void AddStep(int animation, int duration);
    // The last step becomes the open-ended emote `emote` (its variant; duration -1); a blank step is appended
    // first when the list is empty or its last step already carries an emote.
    void AddEmote(int emote);
    void SetStepFlags(int value);
    // The sum of the durations; -1 when one is negative (open-ended).
    int Duration() const;
    // Compares the count, then each step but its padding byte.
    bool SameAs(const AnimationSteps* other) const;
    // The count, each step's 12 bytes, extra. A count the stream did not deliver is 0 (the original's was its
    // stack garbage).
    void Load(CStream* stream);
    void Save(CStream* stream) const;

    int extra = -1;
};

// The plan of an order: its animation, when it starts and ends, and what it aims at.
struct OrderPlan {
    AnimationSteps animation;
    int startTime;              // game time
    int endTime;                // startTime + the animation's duration
    int targetId;               // -1: none
    float rangeSq;              // squared distance the target may move away
    std::uint8_t isCast;
    int hitLocation;            // the body location the attack aims at (1 by default)
    std::uint8_t notSkill;      // order source != 6
    int castKind;               // 8; for a cast: 1 (spell kind 0x28), 0 (0x29) or the
                                // spell prototype's school
    Vector3 direction;          // from the unit to the target

// Takes `steps` as the animation from `start` for `duration` ticks; the other fields are reset to
// OrderPlanReset's values.
    void TakeSteps(AnimationSteps steps, int start, int duration);
};

void OrderPlanReset(OrderPlan* plan);

class CWorldServer;
// Whether the spell may hit: spells whose prototype has needsSight need a line of sight of at least 0.1 from
// `from` (z = the caster's ground + BaseSpeed, a height) to the target (z = its ground + ModeSpeed), or to `at`
// one unit above the water surface when there is no target.
bool SpellHasLineOfSight(CWorldServer* world, CSpell* spell, CUnit* caster, const Vector3* from,
                                    CUnit* target, const Vector3* at);

struct MoveModeState {
    int mode;                   // 0..2 (3 is stored as 2 with `wasMode3`)
    // Set while the unit attacks (SyncMoveModes) and by the AI's suspicion, aggression and fear modes (cleared
    // when they end): mode 2 then takes the guarded walk / cast animations (not a hero's own walk), the unit
    // cannot be backstabbed and does not fall asleep (moveMode2's, read by the hit code and the world update).
    std::uint8_t onGuard;
    std::uint8_t wasMode3;
};

// What a unit attacks with (CUnitServer::GetAttackParams).
struct AttackParams {
    // Unless `keyed`: attack and defence times the difficulty's factors, truncated.
    void ScaleForDifficulty(int keyed);

    int weaponType;             // CItemWeapon::GetType, or the natural weapon
    int damageType;             // 0..6
    float speed;                // the weapon's, or the natural attack's
    float damageMin;            // scaled by the stats' damage scale
    float damageMax;            // damage + spread, scaled likewise
    float attackSpread;         // random range of the attack roll
    int attack;                 // attack skill (truncated)
    int defence;                // defence skill (truncated; 0 for ranged weapons)
};

// A record of the unit's presence with the AI events. AIViewer::StepNoise, which would fill it, always returns
// 0, so none is ever written; the layout is speculative (the fields the controller's update would write).
struct AIEventPresence {
    float x;                    // where the unit was when the record was last refreshed
    float y;
};

// An entry of the armour spell list: the item (a counted reference), its countdown and whether it is still
// worn.
struct ArmourSpell {
// The item's id (-1 without one) and the countdown.
    void Save(CStream* stream) const;
// `other`'s item is referenced before the old one is released.
    ArmourSpell* Assign(const ArmourSpell* other);

    CItem* item;
    int countdown;
    std::uint8_t worn;
};

// `count` entries appended (no item, countdown 0; the original's `worn` byte was stack garbage, ours 0), then
// each entry from the head takes its item (the item manager's object of the id read, referenced) and countdown.
void LoadArmourSpells(std::list<ArmourSpell>* list, CStream* stream);

class CUnitServer : public CUnitController {
public:
    CUnitServer();
    ~CUnitServer() override;

    int Create() override;
    void Destroy() override;
    void ClearState() override;
    int RunTick() override;
    // The controller's state (animation lists, the motion and plan objects, both orders, the stats copy,
    // movement factors, the brain, the voice, the effect object and the attackers list), version dependent from
    // 0x44 on. Save writes the same at the current version.
    void Load(CStream* stream) override;
    void Save(CStream* stream) override;
    const char* GetClassName() override;
    int GetClassId() override;
    PathOrder* Path() override { return &motion; }
    void Unused17() override {}
    int ShowWeapon(std::uint32_t slot) override { return TakeUpWeapon(static_cast<int>(slot)); }
    bool ArmourTakenOff(std::uint32_t unused) override { return RebuildArmourLayers(static_cast<int>(unused)); }
    void ObjectDeleted(CEngineObject* object) override { OnObjectDeleted(object); }

    // The active weapon's reach, else the natural reach.
    float AttackRange();
    BOOL HasRangedWeapon();
    // A ranged weapon, or (without a weapon) a natural weapon other than 0x10 with a reach above 1.
    BOOL AttacksFromRange();
    // out[1..7]: the weapon's (party units) or the natural attack's damage, scaled, times each weight.
    void GetDamageByType(DamageWords* out);
    // out[0] = 0x7FFF, out[1 + the spell's damage type] = its truncated damage, the rest 0.
    void GetSpellDamage(std::uint32_t source, DamageWords* out);
    // The quick item of `source` (high bit ignored) if it carries a spell (class 0x13, category 5 or 8).
    CItem* SpellCarrier(std::uint32_t source);
    bool IsScrollSource(std::uint32_t source);
    // A spell slot's spell, or a carrier item's.
    CSpell* SpellOfSource(std::uint32_t source);
    // Does the current order count as fighting (for the battle music): an attack (kind 3), or a cast (4, 5) of a
    // spell of type 0-8 or 0x1A from a spell slot or a spell carrier (quick item category 5 or 8). Rebirth's
    // FixPlaingBattleMusicFromAntimagic drops 0x1A (antimagic).
    bool IsFighting();
    // A carrier with enough charge (category 5), or a spell the party unit has the mana for.
    bool CanCast(const AIOrder* order);
    // The point of the first move order (action, then action2). 0/1.
    BOOL GetMovePoint(float* out);
    float ActionDistance(const AIOrder* order);
    // False only for a melee order (kind 3 without a ranged weapon, or 6) on a living unit of size class 0
    // whose prototype height plus its z is 1.5 or more.
    bool CanReachTarget(const AIOrder* order);
    // 1 << the weapon type (natural weapon types 7 and above: 0).
    std::uint32_t WeaponTypeMask();
    // Animation bits of a movement mode: 0 0x30000, 1 0x20000, 2 0x10000 (only with
    // `onGuard` or for units other than heroes), 4 0x8000, else 0.
    std::uint32_t MoveModeMask(const MoveModeState* mode);
    // log(experience / considerModifier) / log(considerLogBase), rounded.
    int Level() override;
    // Sets the unit's flags_ bit 0x10000000 when it has an effect of type 0x20 and a natural weapon other than
    // 0x10, else clears it.
    void EffectsChanged() override;
    void GetAttackParams(AttackParams* out) override;
    // Random spread + attack, minus a distance penalty, plus a speed ratio unless `backstab`; at least 0.
    // Draws from the ISAAC generator when the attacker's spread is positive.
    float AttackRoll(CUnitServer* attacker, bool backstab);
    // Decides whether `hit` (from `attacker`, may be null) hits: attackRoll becomes FLT_MAX (hit) or -FLT_MAX
    // (miss). Elemental hits always hit.
    BOOL ResolveHit(DamageInfo* hit, CUnit* attacker);
    // The hit of a weapon (or natural) attack into `out`. Draws from rand() and the ISAAC generator. Returns 1.
    BOOL BuildAttackHit(DamageInfo* out, int backstab, int location, char calledShot);
    // `source` (may be null) is whose player gets the experience for a kill. The damage applies when the hit
    // beats the defence, unless the unit's player protects it (returns 1 then); on a miss the victim turns
    // hostile and remembers the attacker.
    BOOL TakeHit(DamageInfo* hit, CUnit* attacker, CUnit* source) override;
    void MoveFactorsChanged() override;
    // Part 4 changed; the server companion's hit points and mana set to their maxima.
    void SetStats(const UnitStats* stats);
    // Remark `code` (below 0x2A, or 0x30) to the unit's player (message 0x0B); nothing without a player.
    void Shout(std::uint32_t code);
    // The three size classes from the unit's load and size (times the model's factor; heroes capped at 1.89).
    void UpdateSizeClasses() override;
    // The movement factors from the movement mode, the world's factors and the spell being cast, and the speed
    // class (0, 1, 3, 7 or 15 by the unit's speed when idle, else 15).
    void UpdateMoveFactors() override;

    // --- The end of an action, missiles, item spells, death, healing, skills.
    // When the current order's animation reaches its moment: an attack delivers pendingHit, a cast finishes, a
    // skill or use is applied, a quick item of category 2 is used up; all but attacks then clear the order.
    void FinishAction(int location, char unused);
    // A missile (world object 0x5B carrying the hit) for a bow or crossbow, else the target's TakeHit
    // directly; then the weapon's spell, and noise when the hit was not a backstab.
    void DeliverAttack(CMapObject* target, DamageInfo* hit);
    // Casts the active weapon's spell (flag 0x10000) at `target` when its charge covers the cost.
    void TriggerWeaponSpell(CMapObject* target);
    // The spells of the armour pieces that trigger on being hit (`onHit`; spell prototype 0x18 triggers when
    // not hit) and have the charge for it.
    void TriggerItemSpells(char onHit);
    // Also updates the party's copy of the item.
    void UseItemCharge(CItem* item, float cost);
    // Logs "Unit 12 was healed". Returns 1.
    BOOL Heal(float amount) override;
    // Reacts to damage. Not dead: the armour spells, diplomacy, the AI memory, hostility claims and the hit
    // direction. Dead: remarks, witnesses, the death state, "Unit 12 damaged to death", the death animation,
    // the AI notification and the party's reaction. Units of types outside 0x32..0x36 only get flags_ bit 1.
    void OnDamaged(CUnit* attacker, int hitKind);
    // The allied witnesses that see `victim` add `murderer` to their visible list and turn hostile to it.
    void WitnessMurder(std::list<CUnit*>* witnesses, CUnit* victim, CUnit* murderer);
    // When the unit's flags_ have 0x80000: clears it, adds its hostile mask to claimedHostile and swaps
    // hostility claims with the allied units nearby.
    void PropagateHostility();
    // Where `attacker` stands relative to the unit: 0x800000 in front, 0x400000 behind, 0xC00000 / 0x1000000
    // to the sides; 0 without an attacker or with a non-zero `hitKind`.
    std::uint32_t HitDirection(CUnit* attacker, int hitKind);
    // Kind 6 with source 0x18A95 (a talk / trade order), else science (0) and stealing (1).
    void UseSkill(CMapObject* target);
    // Drops the queued order unless it is of kind 7 or 8 or there is no current order; the queued order ends
    // up of kind 0 (sic).
    void Idle();
    // Re-registers the unit on the sector map; dying also removes and releases effectObject.
    void SetDead(int dead);

    // A carrier pays from its charge (not enough: nothing is cast), a party unit from its mana; noise unless
    // the spell's castType is 1.
    void FinishCast();
    // Order setters: each replaces the queued order (action2). Kind 2 (turn) towards `direction`; param =
    // `time`.
    void OrderTurn(const float* direction, std::uint32_t time);
    // Kind 1 (move) to `point`: source = `walk` != 0, modifier = `run`, the point's z replaced by `range`
    // (sic). A party unit with a real point (z not -1313) gets param 1 while the world is not paused, else
    // plans the move within `range`.
    void OrderMove(const Vector3* point, int run, int walk, float range);
    // Kind 3 at body part `location` (6, all, unless the target's part kind is 3); modifier = `calledShot`.
    // Dropped when the target is out of reach (message 0x0C) or the planner refuses.
    void OrderAttack(CMapObject* target, int location, int calledShot, int planArg);
    // Kind 4 (cast at a point). A scroll (quick item of category 8) is cast at once. A party unit needs the
    // spell's skill for a spell of its own; message 0x13 if not, or while flags_ has 0x10000000.
    void OrderCastAt(std::uint32_t source, const Vector3* point, int run);
    void OrderCast(std::uint32_t source, CMapObject* target, int run);
    // Unless the current order is of kind 11: the queued order becomes kind 11 (stop).
    void StopQueued();

    // Would the unit fit its cell in movement mode `mode` (3 counts as 2)? True at once for moveMode's mode;
    // else the unit's mode becomes `mode` for the test (off the sector map) and is put back.
    bool FitsMoveMode(int mode);
    // The unit's wanted movement style (0 lie, 1 crawl, 2 walk, 3 run). When it fits, a party unit hears
    // message 9, unless running and out of breath. Other than walking and running the current order's run flag
    // is cleared.
    void SetMoveStyle(int style);
    void SetWantedWeapon(int slot);
    // A skill or use order (kind 6) on `target`: stealing (source 1; a living other object), taming (2; a
    // living unit of another player, not a hero), use (0, spot = `spot`; any object) and searching a body
    // (0x18A95; a dead object other than the unit). Dropped when the planner (shouting 6 / 5 / 5 / 4) refuses.
    void OrderSteal(CMapObject* target, int run);
    void OrderTame(CMapObject* target, int run);
    void OrderUse(CMapObject* target, int spot, int run);
    void OrderSearchBody(CMapObject* target, int run);
    // Is `target` (a body to search) taken: with flags_ bit 0x20000000, when the world does not share it with
    // this unit's id; else when a living unit of the brain's nearby list is searching it (kind 6, source
    // 0x18A95) in state 5 or with its flag set.
    bool TargetBusy(CMapObject* target);
    // `name` from the figure's animation database as the queued order: kind 8 for keys with bit 31, else 7;
    // param = the entry's index dword.
    void PlayAnimation(const char* name);
    // Drops everything: state 6, the effect object, the plan and both orders; the unit stands still.
    void StopAction();
    // Does the unit wear an armour piece in slots 3..6 / in the first three (body, trousers, helmet)?
    // CUnit::AcceptsTarget asks for the animation bits 0x10 / 0x20 by them.
    BOOL HasOtherArmour() override;
    BOOL HasMainArmour() override;

    // --- The statistics virtuals.
    // The vitals recomputed (with `withHitPoints` the hit points first) in the server companion's block; a
    // pending attack time is rescaled by the old / new attack rate, rounded; part 0 changed. Returns 1.
    BOOL RefreshVitals(char withHitPoints) override;
    // Protection[1..7] from the body and protection[0], the defence: a party unit with a weapon its weapon
    // skill bonus, others their defence times the difficulty's factor; truncated.
    void UpdateProtection();
    // The statistics repacked from the parameters, the hit flags cleared, the protection updated, part 4 changed;
    // motionChanged set when the movement speeds changed.
    void RepackStats() override;
    // The armour piece the unit wears in the slot of armour prototype `key >> 16` (its type) added to the
    // layers. Returns false.
    bool ArmourPutOn(std::uint32_t key) override;
    // The armour layers rebuilt (the argument unread; returns true) / the outer layers cleared (returns
    // UnitStats::ClearOuterLayers's 0).
    bool RebuildArmourLayers(int unused);
    int ClearOuterArmourLayers() override;
    BOOL RefreshEquipment() override;
    // The claims dropped (claimedHostile) and the calls heard with a hostility mask.
    void HostileMaskChanged() override;
    void CopyStats(UnitStats* out);
    // RefreshVitals(0) after (only CUnitServer's vtable has it), whose 1 it returns.
    BOOL AddExperience(float amount);

    // Kind 9 and cleared, then kind 0 (Idle and FinishAction).
    void DropQueuedOrder();

    // What a cast (kinds 4, 5) or an order on a unit must see: false for an order to a use spot (pointIsUseSpot), a missing
    // spell, a spell whose prototype lacks needsSight, or (other kinds) a target that is not a unit.
    bool FindSightTarget(const AIOrder* order, struct SightTarget* out);
    // Can the unit carry out `order` at `time` with its target at `point`: in range for AI units, no living
    // unit in the way of a melee or skill order, and a line of sight when the order needs one.
    bool CanActFrom(const AIOrder* order, const float* point, int time);
    // Builds the animation (which draws random numbers), then looks for the first moment at which the target
    // is in reach, the unit can turn to it in time, and CanActFrom holds; fills `plan` then.
    bool PlanAction(const AIOrder* order, struct OrderPlan* plan);
    // With the target unit's flags_ bit 0x8000000 set meanwhile.
    bool CanPlanAction(const AIOrder* order);
    // False for an attack without a ranged weapon, or a skill, on a living unit of size class 0 that is too
    // high to reach (its prototype's height plus its z at 1.5 or above); else CanPlanAction.
    bool CanReachAndPlan(const AIOrder* order);
    bool CanReachAndPlanFrom(const AIOrder* order, const Vector3* point);

    // --- The animation steps of an order. The unit's animation entries are looked up by a key of weapon,
    // movement and action bits.
    // The entry's values divided by the unit's speed factor for it, truncated; 0 without the entry.
    int AnimationDuration(std::uint32_t index);
    int AnimationMoment(std::uint32_t index);
    // The step that changes the movement mode `from` into `to` (only heroes, or a change involving mode 4).
    bool BuildTransition(struct AnimationSteps* steps, const MoveModeState* from, const MoveModeState* to);
    // For kind 3 `source` is the attack key from BuildAttackSteps. Skills are framed by stepping out of and
    // back into the movement mode; *duration = the ticks until the action happens.
    void BuildOrderSteps(struct AnimationSteps* steps, int* duration, int kind, std::uint32_t source);
    // Draws a random number for heroes.
    void BuildCastSteps(struct AnimationSteps* steps, int* duration, int kind, int value);
    // The hit location (plan->hitLocation) and the height of the blow from both units' heights and the
    // distance. Draws random numbers. Returns 1.
    bool BuildAttackSteps(struct OrderPlan* plan, struct AnimationSteps* steps, int* duration, CMapObject* target);

    // Every tick: takes over the queued order, else follows the path and continues the current order
    // (ContinueAction for actions; a finished move or animation lets the queue on).
    void UpdateOrders();
    bool StartQueuedOrder();
    // Kind 1: plans a path with the units near the goal lifted off the sector map; done orders let the next
    // one start.
    bool StartMove();
    // Continues an action order while its target is valid and seen, else ends it (party messages 0x0B / 0x0E /
    // 0x12).
    void ContinueAction();
    // When the action can start, rolls an attack's hit and starts it, else closes in (ApproachTarget) or
    // drops the order.
    void ExecuteOrder();
    // Waits when in reach, else plans a path to where the target will be.
    void ApproachTarget();
    // moveMode / moveMode2 from the unit's modes and the order.
    void SyncMoveModes();
    // StartQueuedOrder's common start.
    void TakeQueuedOrder();
    void SetUnitMoveMode2(int mode);

    // --- The update and the animation queue.
    // Once per world update. A living unit runs its regeneration, AI, item spells, orders, state and animation;
    // a dead one sinks to the ground and, once its death animation is over, reports its death and drops its
    // orders. States: 0 idle, 1 moving, 2 / 4 a hit reaction, 3 the rest of an action's animation then 6, 5
    // TickAction. Returns 1.
    void UpdateState();
    // Moves the queue on to the world's time and shows its current step on the unit; frozen or reacting units
    // keep their times shifted.
    void AdvanceAnimation();
    // Drops the steps that are over (a pause after one takes its animation over); true when one was dropped.
    bool PopFinishedSteps();
    void ShowCurrentStep();
    void PlaySteps(const AnimationSteps* steps);
    // Other than 5 (acting) the size class is updated and the effect object dropped; 5 sets actingCheckTime to
    // 10 updates from now.
    void SetState(int newState);
    // Re-registers the unit on the sector map.
    void SetStanding(int standing);
    // The unit stays where it is, facing its direction; standing 0.
    void StandStill();
    // The reaction to a hit from hitDirection (BuildDeathAnimation without dying), saving the queue in
    // savedQueue, unless the unit is idle or the reaction would delay the next attack.
    void PlayHitReaction();
    // Aborted (or not started, or with an open-ended animation) the state becomes 6, else 3 with what is left
    // of a three-step plan played; the plan is reset and the unit stands still.
    void EndOrder(char abort);
    // State 5: starts the planned action at its time and finishes it at the end (FinishAction).
    void TickAction();
    // Casting allowed, the target (or a spell's point) not dead and within the plan's range at its end time.
    bool ActionInReach();
    // Each armour spell casts on the unit when its countdown runs out and the item's charge covers it (type
    // 0x18: one a tick, when hurt).
    void TickItemSpells();

    // False while an attack turns (state 2) and `ticks` from now would pass nextAttackTime.
    bool CanReactFor(int ticks);
    // Play `steps` as an animation order (state 3) / an emote (state 4): the queue takes a copy, the unit stops
    // facing its direction.
    void PlayAnimationOrder(const AnimationSteps& steps);
    void PlayEmoteOrder(const AnimationSteps& steps);
    // The emote 0xFE for a hero walking (mode 2 without onGuard), else the idle entry of the weapon and movement
    // key (| 0x80580000), if any.
    void BuildIdle(AnimationSteps* steps);
    AnimationSteps* IdleAnimation();
    // Unless idle, moving or turning: the idle animation, rebuilt when needed, plays.
    void PlayIdle();
    // The movement animation of the current mode: looped (an emote of the entry; a hero's by stamina) or `once`
    // (the entry's duration); every step's kind is 2.
    void BuildMoveSteps(AnimationSteps* steps, char once);
    // For a hero the start once, then the loop.
    AnimationSteps* NextAnimation();
    // State 5: the steps get `stepFlags` in byte 2; the motion turns to the plan's direction or follows its
    // target (until its end when `follow`).
    void StartAction(char follow, char stepFlags);
    // moveMode's mode 3 (wasMode3) while moveMode2 asks for it: taken on a cheap cell when not out of breath
    // (and, without a player's permission, with stamina); dropped after 5 updates otherwise; out of breath
    // drops moveMode2's too (remark).
    void UpdateSneak();
    // moveMode2 becomes moveMode unless the unit no longer fits its cell (message 0x0D); a transition animation
    // plays (not while attacking). True when the mode changed.
    bool ChangeMoveMode();
    // FindPath within `range`: no path stands the unit still (state 6, stoodBlocked from the path's failed
    // flag); a path makes it idle (state 0) with its movement animation going on.
    bool PathTo(const Vector3* point, float range);
    // Fills `list` with the units lifted off the sector map for a path: away from `point`, those near it
    // outside the player's group; the party members that move (with `moving`) or follow this unit. Only for a
    // player's units with sessionKind100_ 1.
    void LiftNearby(std::list<CUnit*>* list, const Vector3* point, char moving);
    // Messages 0x0E / 0x01 to a party unit's player, unless the brain's mode is 6.
    void ReportPath(AIPath* path, char partial);
    // Every field to its start value (constructor and Load).
    void ResetController();
    // Drops the orders and takes the movement records from the unit's modes; the wanted weapon is taken up
    // and, unless turning, the idle animation plays (a random phase on an empty queue).
    void ResyncWithUnit();
    // A tame attempt by `tamer` with `roll`: above 10 x the taming virtual plus the taming skill (stats value
    // 2) it moves the unit a stage along (1 not hostile to the tamer's side, 2 on that side, 3 in the tamer's
    // party with a tamed mode); a unit with a player or a failed roll is attacked instead (taming by that side
    // undone). 0/1.
    BOOL TryTame(CUnit* tamer, float roll);
    // The tamed mode (8) removed, no player, all relations, the old side back (from stage 2), stage 0.
    void UndoTaming();
    // From the items the unit wears (weapons only the one in use) whose spell is periodic: kept entries keep
    // their countdown, new ones start at 0, entries of items no longer worn go.
    void RefreshItemSpells();

    // --- The way to an order's target.
    // For a use order on a target (kind 6, source 0): the reachable spot the target offers (for the unit's
    // size) whose path is cheapest becomes the order's point (pointIsUseSpot set), its path into `motionOrder` (if
    // given); false when no spot can be reached. Every order's point gets z -1 first.
    bool FindUseSpot(UnitAction* order, PathOrder* motionOrder);
    // Simulates the unit along `motionOrder` (a path order, kind 2) in steps of 5 ticks until the order's
    // target is in reach (and seen, when the order needs a line of sight); 0 for other kinds.
    double TimeUntilAction(const AIOrder* order, PathOrder* motionOrder);
    // The use spot, the path (target lifted off the sector map) and whether the unit can act from its end,
    // with the player's path message; the unit shouts `shout` when it can, 0x0B / 0x0E when not. Units without
    // a player plan only use orders.
    bool PlanOrderRoute(UnitAction* order, std::uint32_t shout);
    // A party unit's move: the units in the way lifted (LiftNearby), a runner planning as walking where it can,
    // FindPath within `range` (the order's z bits), the units put back and the result reported.
    void PlanMove(const Vector3* point, int moving, int run, float range);

    // `claimant` claims the players in `mask` are enemies of this unit: they join claimedHostile and the AI
    // memory hears a call of kind 0x1E carrying the mask.
    void ClaimHostility(CUnit* claimant, std::uint32_t mask);
    // The weapon in `slot` (-1, none, when the slot is empty or the weapon's condition rounds to 0 or less)
    // becomes the active and wanted weapon (part 6 changed); a party unit's AI options are rebuilt, the armour
    // spells refreshed. 1 when the weapon was taken up.
    BOOL TakeUpWeapon(int slot);
    // Orders aimed at `object` end (the current one reset, then Idle when the queued one is empty; a queued one
    // is emptied unless an animation), a plan aimed at it ends (EndOrder(0)), the brain forgets it and the
    // leader followed is dropped when it is that object.
    void OnObjectDeleted(CEngineObject* object);
    // A living unit is set up again where it stands: off the sector map, its figure, size classes and size
    // class; when it cannot leave its cell it moves to a free cell near it, or with none it takes a default hit
    // (a unit that cannot stand dies); then back on the sector map.
    void Resettle() override;
    int TamingStage() override;

    // The death (or fall) animation for a blow from `direction` (HitDirection's bits): `dying` adds the death
    // bits, `fall` (lying down) stops the steps with kind 2 and may pick a side at random. Only the low bytes of
    // `dying` and `fall` count.
    void BuildDeathAnimation(struct AnimationSteps* steps, std::uint32_t direction, int dying, int fall);

    std::uint8_t frozen;            // the unit is frozen: the animation and the orders wait (UpdateState)
    std::uint8_t animationHeld;     // a hit reaction plays (or no size classes): the times shift
    AnimationSteps queue;           // extra: when its first step started, -1 until then
    AIPath motion;
    OrderPlan currentPlan;
    DamageInfo pendingHit;          // the hit of the attack in progress
    std::uint8_t keepHitDirection;  // while set, OnDamaged leaves hitDirection alone
    std::uint8_t stoodBlocked;      // the unit stood still because its path search failed
    std::uint8_t motionChanged;     // set when the movement speeds or mode changed (cleared by PathTo)
    std::uint8_t movingAnimation;   // the movement animation is in the queue (cleared when no
                                    // path is found)
    AnimationSteps savedQueue;      // the queue a hit reaction saved (its extra is its time)
    std::uint32_t hitDirection;     // HitDirection of the last hit that did not kill
    int state;                      // 0 idle, 5 casting, ...
    MoveModeState moveMode;
    MoveModeState moveMode2;
    int sneakWait;                  // updates mode 3 was kept without a cheap cell (UpdateSneak)
    CEngineObject* effectObject;    // (referenced) removed from the world on death
    int actingCheckTime;            // world tick + 10 when acting starts (StartAction), -1 reset
    int nextAttackTime;             // world tick of the next attack
    int interceptTime;              // the tick a chase aims at (the target's position then); saved
    std::uint8_t orderStopped;      // set when a move order ends, cleared when one starts; saved; the AI
                                    // brain reports its target only while it is set (unit_ai.cpp)
    std::uint8_t deathReported;     // the death was reported to the world (the dead unit's update)
    std::uint8_t searchingTarget;   // TargetBusy counts a search under way when set (or in state 5)
    UnitAction action;
    UnitAction action2;
    std::uint8_t followsLeader;     // the chase follows `followedLeader`'s target (combat_tick.cpp)
    CUnit* followedLeader;          // (referenced) the party unit whose attack this one joins
    UnitStats stats;                // its flags get bit 0x10 on a hit
    DamageWords protection;         // [0] the defence value, [1..7] the body's protection
                                    // per damage type (UnitStats::PackBodyProtection)
    float sinkSpeed;                // a dead unit's body sinks this much a tick (accelerating)
    float moveFactors[4];
    int tameStage;                  // 0..3 (TryTame)
    int ownSide;                    // the unit's own side while tamed (TryTame)
    int tamerSide;                  // the tamer's side (TryTame)
    int speedClass;
    AIEventPresence* aiPresence;    // StepNoise's result, always 0
    std::list<ArmourSpell> armourSpells;   // periodic spells of the worn items (blocks of 10)
    int boredTicks;                 // a party unit's updates since its last order (a bored remark past a
                                    // limit); cleared when an order starts and by the move style /
                                    // wanted weapon setters
    int waitFrame;                  // a frame number; the movement tick waits for a party member
                                    // it blocks while the frame is below this + 2
    std::uint32_t moveAnimationKey; // 0x666DEAD: none yet
    AnimationSteps moveAnimation;   // the movement animation (NextAnimation)
    std::uint32_t idleKey;          // 0x666DEAD: none yet
    AnimationSteps idleAnimation;   // extra: its time
    void(*actionDone)(CUnit*);   // run when state 3 ends; cleared by the reset
    std::uint32_t claimedHostile;   // players the unit has claimed as enemies
    AIBrain brain;
    AIAlarm voice;                  // the voice / alarm


    void AddHostileMask(std::uint32_t mask);
};

inline CUnitServer* CUnit::ServerController() const { return static_cast<CUnitServer*>(controller); }

