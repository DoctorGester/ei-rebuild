#pragma once

// Combat: the server-side unit controller (UnitServer, class id 0x3E). Every creature on the server has one;
// it keeps the unit's computed statistics, its current orders, and the attack / hit code.

#include "game/units/ack_kind.h"
#include "game/ai/ai_alarm.h"
#include "game/spells/spell.h"
#include "game/ai/unit_ai.h"
#include "game/spells/spell_db.h"
#include "lib/enum_array.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// A melee target whose prototype height plus its z reaches this is too high to hit (TooHighToReach).
constexpr float kMeleeReachHeight = 1.5f;

// An order's source with this bit is a quick item slot (the low bits), else a spell slot.
inline constexpr std::uint32_t kQuickItemSource = 0x80000000u;

// An order as the controller's order checks take it.
struct AIOrder {
    // UnitOrderKind::None: the object released and cleared, the flag cleared.
    void Clear();
    // Clear, then an attack on `object` (referenced) at every body part (kAttackAllParts).
    void MakeAttack(MapObject* object);
    // Clear, then a cast of `spell` on `object` (referenced).
    void MakeCast(int spell, MapObject* object);
    // Clear, then a use of `object` with `what`.
    void MakeUse(MapObject* object, UseSource what);
    // `source` read as a use order's skill (kind Use).
    UseSource Skill() const { return static_cast<UseSource>(source); }
    // Where the order aims: the object's AimPoint, or `point` without an object.
    Vector3 AimPoint() const;

    UnitOrderKind kind = UnitOrderKind::None;
    std::uint32_t source = 0;       // the body part of an attack (kAttackAllParts), the spell of a cast, the UseSource of a use
    Ref<MapObject> target;
    Vector3 point{};                // target point
    // `point` is a use spot of the target that FindUseSpot picked: the unit walks to it and acts from there
    // (no sight or range checks against the target; reach 0.2).
    bool pointIsUseSpot = false;
};

// An order of the unit: an AIOrder and two more fields. The order checks (CanReachAndPlan and the planning
// under it) take the AIOrder part only: the AI builds just that on the stack.
struct UnitAction : AIOrder {
    int param = 0;              // by kind: a use's spot (Use), a wait's time, an emote's animation,
                                // the studied item's id
    std::uint8_t modifier = 0;  // run (move orders) or a called shot (attacks)

// The target is referenced.
    UnitAction* Assign(const UnitAction* from);
// Kind None, the target released, pointIsUseSpot, param and modifier cleared.
    void Reset();
    void Retarget(MapObject* object) {
        if (object == target) return;
        target.Assign(object);
    }
};

// One step of an animation list: animation is the index of the unit's animation entry (0xFF: a pause), variant
// the emote / animation variant (0xFF: none), flags UnitSetAnimation's flags, a padding byte, duration in game
// ticks, kind (1 holds the queue time, 2 lying, a fall; Unit::SetMotionMode). A blank step is 0xFF, 0xFF, 0,
// duration -1, kind 0. The padding byte is never set: the original's steps carried a stack byte there, ours
// carry 0.
struct AnimationStep {
    static constexpr std::uint8_t kNone = 0xFF;   // no animation (a pause) / no variant

    // Neither an animation nor a variant (nothing to show).
    bool IsBlank() const { return animation == kNone && variant == kNone; }

    std::uint8_t animation = kNone;
    std::uint8_t variant = kNone;
    std::uint8_t flags = 0;
    std::uint8_t padding = 0;
    int duration = -1;
    int kind = 0;
};
static_assert(sizeof(AnimationStep) == 0xC);   // saved as its 12 bytes

// A step list and a dword copied along with it (extra: the queue's start time, ...).
struct AnimationSteps {
    // Appends a blank step of `animation` (low byte) lasting `duration`.
    void AddStep(int animation, int duration);
    // The last step becomes the open-ended emote `emote` (its variant; duration -1); a blank step is appended
    // first when the list is empty or its last step already carries an emote.
    void AddEmote(int emote);
    // The sum of the durations; -1 when one is negative (open-ended).
    int Duration() const;
    // Compares the count, then each step but its padding byte.
    bool SameAs(const AnimationSteps* other) const;
    // The count, each step's 12 bytes, extra. A count the stream did not deliver is 0 (the original's was its
    // stack garbage).
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;

    std::vector<AnimationStep> steps;
    int extra = -1;
};

// OrderPlan::rangeSq of no plan, and the limit of a path search that has none.
inline constexpr float kNoPlanRange = 1e38f;
inline constexpr float kNoPathLimit = 1e6f;

// The plan of an order: its animation, when it starts and ends, and what it aims at.
struct OrderPlan {
    AnimationSteps animation;
    int startTime = 0;          // game time
    int endTime = 0;            // startTime + the animation's duration
    int targetId = -1;          // -1: none
    float rangeSq = kNoPlanRange;   // squared distance the target may move away
    std::uint8_t isCast = 0;
    int hitLocation = 1;        // the body location the attack aims at
    std::uint8_t calledShot = 0;   // the attack aims at one body part (the order's source is not kAttackAllParts)
    SpellSchool castKind = SpellSchool::None;   // for a cast: its record's cast_school, else its school
    Vector3 direction{};        // from the unit to the target

    // The plan of nothing: every field to its start value but the direction, which stays (it is saved).
    void Reset();
    // Takes `steps` as the animation from `start` for `duration` ticks; the other fields are reset.
    void TakeSteps(AnimationSteps steps, int start, int duration);
};

class WorldServer;
// Whether the spell may hit: spells whose prototype has needsSight need a line of sight of at least 0.1 from
// `from` (z = the caster's ground + BaseSpeed, a height) to the target (z = its ground + ModeSpeed), or to `at`
// one unit above the water surface when there is no target.
bool SpellHasLineOfSight(WorldServer* world, Spell* spell, Unit* caster, const Vector3& from,
                                    Unit* target, const Vector3& at);

struct MoveModeState {
    MoveMode mode = {};         // never Run: that is stored as Walk with `wasMode3`
    // Set while the unit attacks (SyncMoveModes) and by the AI's suspicion, aggression and fear modes (cleared
    // when they end): Walk then takes the guarded walk / cast animations (not a hero's own walk), the unit
    // cannot be backstabbed and does not fall asleep (moveMode2's, read by the hit code and the world update).
    std::uint8_t onGuard = 0;
    std::uint8_t wasMode3 = 0;
    std::uint16_t unused = 0;   // saved: the state is written whole
};

// AttackParams::damageType of a natural attack whose damage weights are all zero.
inline constexpr int kNoDamageType = -1;

// What a unit attacks with (UnitServer::GetAttackParams).
struct AttackParams {
    // Unless `keyed`: attack and defence times the difficulty's factors, truncated.
    void ScaleForDifficulty(int keyed);

    WeaponType weaponType = {};  // ItemWeapon::GetType, or the natural weapon
    int damageType = 0;         // 0..6, or kNoDamageType
    float speed = 0.0f;         // the weapon's, or the natural attack's
    float damageMin = 0.0f;     // scaled by the stats' damage scale
    float damageMax = 0.0f;     // damage + spread, scaled likewise
    float attackSpread = 0.0f;  // random range of the attack roll
    int attack = 0;             // attack skill (truncated)
    int defence = 0;            // defence skill (truncated; 0 for ranged weapons)
};

// An entry of the armour spell list: the item (a counted reference), its countdown and whether it is still
// worn.
struct ArmourSpell {
    // The item's id (-1 without one) and the countdown. Load takes the item manager's object of the id,
    // referenced.
    void Load(ByteStream* stream);
    void Save(ByteStream* stream) const;

    HeldRef<Item> item;
    int countdown = 0;
    std::uint8_t worn = 0;
};

// What a unit controller is doing (UnitServer::state).
enum class UnitState : int {
    Idle = 0,        // no order acts: the motion runs (a changed one plans its path first)
    Moving = 1,      // the motion runs
    Turning = 2,     // turning towards a direction or an attack's target
    Animating = 3,   // an animation order, or the rest of an action's animation, plays
    Emote = 4,       // an emote plays
    Acting = 5,      // the planned action (attack, cast, use) runs
    Stopped = 6,     // the unit stands: the order ended or was dropped, ready for the next one
};

// The slots of UnitServer::moveFactors: what the light, the weather and the unit's pose make of its senses. Each
// multiplies one Sense of the unit: noise its conspicuity[Sense::Hearing], hearing its perception[Sense::Hearing],
// visibility its conspicuity[Sense::Sight], sight its perception[Sense::Sight].
enum class MoveFactor : int {
    Noise = 0,
    Hearing = 1,
    Visibility = 2,
    Sight = 3,
};

class UnitServer : public UnitController {
public:
    UnitServer();
    ~UnitServer() override;
    UnitServer* ServerSide() override { return this; }

    void Create() override;
    void Destroy() override;
    void ClearState() override;
    bool RunTick() override;
    // The controller's state (animation lists, the motion and plan objects, both orders, the stats copy,
    // movement factors, the brain, the voice, the effect object and the attackers list), version dependent from
    // 0x44 on. Save writes the same at the current version.
    void Load(ByteStream* stream) override;
    void Save(ByteStream* stream) override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    PathOrder* Path() override { return &motion; }
    int ShowWeapon(std::uint32_t slot) override { return TakeUpWeapon(static_cast<int>(slot)); }
    // The armour layers rebuilt (the slot unread).
    void ArmourTakenOff(std::uint32_t) override { stats.RebuildArmourLayers(unit); }
    void ObjectDeleted(EngineObject* object) override { OnObjectDeleted(object); }

    // The active weapon's reach, else the natural reach.
    float AttackRange();
    bool HasRangedWeapon();
    // A ranged weapon, or (without a weapon) a natural weapon other than SpellsOnly with a reach above 1.
    bool AttacksFromRange();
    // out[1..7]: the weapon's (party units) or the natural attack's damage, scaled, times each weight.
    void GetDamageByType(DamageWords* out);
    // out[0] = 0x7FFF, out[1 + the spell's damage type] = its truncated damage, the rest 0.
    void GetSpellDamage(std::uint32_t source, DamageWords* out);
    // The quick item of `source` (kQuickItemSource ignored) if it carries a spell (a wand or a potion).
    Item* SpellCarrier(std::uint32_t source);
    // `source` is a potion (a quick item of QuickItemCategory::Potion: the AI brain does not take it for a cast).
    bool IsPotionSource(std::uint32_t source);
    // A spell slot's spell, or a carrier item's.
    Spell* SpellOfSource(std::uint32_t source);
    // Does the current order count as fighting (for the battle music): an attack, or a cast of a fighting spell
    // (Spell::Use; FireArrow to AcidFog of the game's) from a spell slot or a spell carrier.
    bool IsFighting();
    // A carrier with enough charge (a wand), or a spell the party unit has the mana for.
    bool CanCast(const AIOrder* order);
    // The current or the queued order is of `kind`.
    bool HasOrder(UnitOrderKind kind) const { return action.kind == kind || action2.kind == kind; }
    // The point of the first move order (action, then action2); none without one.
    std::optional<Vector3> GetMovePoint();
    float ActionDistance(const AIOrder* order);
    // True only for a melee order (an Attack without a ranged weapon, or a Use) on a living unit of size class 0
    // whose prototype height plus its z is kMeleeReachHeight or more.
    bool TooHighToReach(const AIOrder* order);
    // The animation bit of the weapon type (natural weapon types from kAnimatedWeaponTypes: 0).
    std::uint32_t WeaponTypeMask();
    // Animation bits of a movement mode: Lie, Crawl, Walk (only with `onGuard` or for units other than heroes) and
    // Stop have theirs; else 0.
    std::uint32_t MoveModeMask(const MoveModeState* mode);
    // log(experience / considerModifier) / log(considerLogBase), rounded.
    int Level() override;
    // Sets MapObjectFlag::NoCasting when the unit's effects give NoCasting (feeblemind's) and its natural weapon is
    // not SpellsOnly, else clears it.
    void EffectsChanged() override;
    void GetAttackParams(AttackParams* out) override;
    // Random spread + attack, minus a distance penalty, plus a speed ratio unless `backstab`; at least 0.
    // Draws from the ISAAC generator when the attacker's spread is positive.
    float AttackRoll(UnitServer* attacker, bool backstab);
    // Decides whether `hit` (from `attacker`, may be null) hits: attackRoll becomes FLT_MAX (hit) or -FLT_MAX
    // (miss). Elemental hits always hit.
    bool ResolveHit(DamageInfo* hit, Unit* attacker);
    // The hit of a weapon (or natural) attack into `out`. Draws from rand() and the ISAAC generator.
    void BuildAttackHit(DamageInfo* out, int backstab, int location, char calledShot);
    // `source` (may be null) is whose player gets the experience for a kill. The damage applies when the hit
    // beats the defence, unless the unit's player protects it; on a miss the victim turns hostile and remembers
    // the attacker.
    void TakeHit(DamageInfo* hit, Unit* attacker, Unit* source) override;
    void MoveFactorsChanged() override;
    // Part 4 changed; the server companion's hit points and mana set to their maxima.
    void SetStats(const UnitStats* stats);
    // The remark `kind` (a shoutable one) to the unit's player; nothing without a player.
    void Shout(AckKind kind);
    // The three size classes from the unit's load and size (times the model's factor; heroes capped at 1.89).
    void UpdateSizeClasses() override;
    // The movement factors from the movement mode, the world's factors and the spell being cast, and the speed
    // class (0, 1, 3, 7 or 15 by the unit's speed when idle, else 15).
    void UpdateMoveFactors() override;

    // --- The end of an action, missiles, item spells, death, healing, skills.
    // When the current order's animation reaches its moment: an attack delivers pendingHit, a cast finishes, a
    // skill or use is applied, a Consumable quick item is used up; all but attacks then clear the order.
    void FinishAction(int location);
    // A missile (an EffectArrowFigure carrying the hit) for a bow or crossbow, else the target's TakeHit
    // directly; then the weapon's spell, and noise when the hit was not a backstab.
    void DeliverAttack(MapObject* target, DamageInfo* hit);
    // Casts the active weapon's spell (SpellTargetFlag::ItemTriggered) at `target` when its charge covers the cost.
    void TriggerWeaponSpell(MapObject* target);
    // The spells of the armour pieces that trigger on being hit (`onHit`; a spell whose record has item_heals when
    // not hit) and have the charge for it.
    void TriggerItemSpells(char onHit);
    // Also updates the party's copy of the item.
    void UseItemCharge(Item* item, float cost);
    // Logs "Unit 12 was healed". Returns 1.
    bool Heal(float amount) override;
    // Reacts to damage. Not dead: the armour spells, diplomacy, the AI memory, hostility claims and the hit
    // direction. Dead: remarks, witnesses, the death state, "Unit 12 damaged to death", the death animation,
    // the AI notification and the party's reaction. Objects outside Hero to Building only get MapObjectFlag::Removed.
    void OnDamaged(Unit* attacker, int hitKind);
    // The allied witnesses that see `victim` add `murderer` to their visible list and turn hostile to it.
    void WitnessMurder(const HeldUnits* witnesses, Unit* victim, Unit* murderer);
    // When the unit has MapObjectFlag::RelationsChanged: clears it, adds its hostile mask to claimedHostile and swaps
    // hostility claims with the allied units nearby.
    void PropagateHostility();
    // Where `attacker` stands relative to the unit: Front, Back, MinusX or PlusX; None without an attacker or with a
    // non-zero `hitKind`.
    BlowSide HitDirection(Unit* attacker, int hitKind);
    // A UnitOrderKind::Use: UseSource::SearchBody loots the body; else science (UseSource::Use) and stealing.
    void UseSkill(MapObject* target);
    // Drops the queued order unless it is an Animation or an Emote or there is no current order; the queued order
    // ends up of kind Idle (sic).
    void Idle();
    // The current order is over: reset, and Idle() unless another is queued.
    void EndCurrentOrder() {
        action.Reset();
        if (action2.kind == UnitOrderKind::None) Idle();
    }
    // Re-registers the unit on the sector map; dying also removes and releases effectObject.
    void SetDead(int dead);

    // A carrier pays from its charge (not enough: nothing is cast), a party unit from its mana; noise unless
    // the spell's castType is 1.
    void FinishCast();
    // Order setters: each replaces the queued order (action2). A Turn towards `direction`; param = the bits of
    // `time` (sic: the AI passes a turning time as a float and the turn reads the bits as its tick count).
    void OrderTurn(const Vector3& direction, float time);
    // A Move to `point`: source = `walk` != 0, modifier = `run`. A party unit with a real point (z not -1313)
    // gets param 1 while the world is not paused, else plans the move at once (PlanMove).
    void OrderMove(const Vector3& point, int run, int walk);
    // An Attack at body part `location` (kAttackAllParts, unless the target's part kind is 3); modifier =
    // `calledShot`. Dropped when the target is out of reach (AckKind::OutOfReach) or the planner refuses.
    void OrderAttack(MapObject* target, int location, int calledShot, AckKind planArg);
    // A CastAt a point. A potion is cast at once. A party unit needs the spell's skill for a spell of its own;
    // AckKind::SpellTooComplex if not, or while the unit has MapObjectFlag::NoCasting.
    void OrderCastAt(std::uint32_t source, const Vector3& point, int run);
    void OrderCast(std::uint32_t source, MapObject* target, int run);
    // Unless the current order is a Stop: the queued order becomes a Stop.
    void StopQueued();

    // Would the unit fit its cell in movement mode `mode` (Run counts as Walk)? True at once for moveMode's mode;
    // else the unit's mode becomes `mode` for the test (off the sector map) and is put back.
    bool FitsMoveMode(MoveMode mode);
    // The unit's wanted movement style. When it fits, a party unit hears AckKind::ChangePosition, unless running
    // and out of breath. Other than walking and running the current order's run flag is cleared.
    void SetMoveStyle(MoveMode style);
    void SetWantedWeapon(int slot);
    // A Use order on `target`: stealing (UseSource::Steal; a living other object), taming (Tame; a living unit of
    // another player, not a hero), use (Use, spot = `spot`; any object) and searching a body (SearchBody; a dead
    // object other than the unit). Dropped when the planner (shouting Steal / UseObject / UseObject / Loot) refuses.
    void OrderSteal(MapObject* target, int run);
    void OrderTame(MapObject* target, int run);
    void OrderUse(MapObject* target, int spot, int run);
    void OrderSearchBody(MapObject* target, int run);
    // Is `target` (a body to search) taken: with MapObjectFlag::Shared, when the world does not share it with
    // this unit's id; else when a living unit of the brain's nearby list is searching it (UnitOrderKind::Use,
    // UseSource::SearchBody) while Acting or with its flag set.
    bool TargetBusy(MapObject* target);
    // `name` from the figure's animation database as the queued order: an Emote for keys with bit 31, else an
    // Animation; param = the entry's index dword.
    void PlayAnimation(std::string_view name);
    // Drops everything: Stopped, the effect object, the plan and both orders; the unit stands still.
    void StopAction();
    // Does the unit wear an armour piece in slots 3..6 / in the first three (body, trousers, helmet)?
    // Unit::AcceptsTarget asks for the animation bits 0x10 / 0x20 by them.
    bool HasOtherArmour() override;
    bool HasMainArmour() override;

    // --- The statistics virtuals.
    // The vitals recomputed (with `withHitPoints` the hit points first) in the server companion's block; a
    // pending attack time is rescaled by the old / new attack rate, rounded; part 0 changed.
    void RefreshVitals(bool withHitPoints) override;
    // Protection[1..7] from the body and protection[0], the defence: a party unit with a weapon its weapon
    // skill bonus, others their defence times the difficulty's factor; truncated.
    void UpdateProtection();
    // The statistics repacked from the parameters, the hit flags cleared, the protection updated, part 4 changed;
    // motionChanged set when the movement speeds changed.
    void RepackStats() override;
    // The armour piece the unit wears in the slot of armour prototype `key >> 16` (its type) added to the
    // layers.
    void ArmourPutOn(std::uint32_t key) override;
    void ClearOuterArmourLayers() override { stats.ClearOuterLayers(); }
    void RefreshEquipment() override;
    // The claims dropped (claimedHostile) and the calls heard with a hostility mask.
    void HostileMaskChanged() override;
    void CopyStats(UnitStats* out);
    // The stats gain `amount` experience, then RefreshVitals(false).
    void AddExperience(float amount);

    // Kind None and cleared, then kind Idle (Idle and FinishAction).
    void DropQueuedOrder();

    // What a cast (CastAt, CastOn) or an order on a unit must see: false for an order to a use spot (pointIsUseSpot), a missing
    // spell, a spell whose prototype lacks needsSight, or (other kinds) a target that is not a unit.
    bool FindSightTarget(const AIOrder* order, struct SightTarget* out);
    // Can the unit carry out `order` at `time` with its target at `point`: in range for AI units, no living
    // unit in the way of a melee or skill order, and a line of sight when the order needs one.
    bool CanActFrom(const AIOrder* order, const Vector3& point, int time);
    // Builds the animation (which draws random numbers), then looks for the first moment at which the target
    // is in reach, the unit can turn to it in time, and CanActFrom holds; fills `plan` then.
    bool PlanAction(const AIOrder* order, struct OrderPlan* plan);
    // With the target unit's MapObjectFlag::Planned set meanwhile.
    bool CanPlanAction(const AIOrder* order);
    // False for a melee order (TooHighToReach) on a unit that is too high to reach; else CanPlanAction.
    bool CanReachAndPlan(const AIOrder* order);
    bool CanReachAndPlanFrom(const AIOrder* order, const Vector3& point);

    // --- The animation steps of an order. The unit's animation entries are looked up by a key of weapon,
    // movement and action bits.
    // The entry's values divided by the unit's speed factor for it, truncated; 0 without the entry.
    int AnimationDuration(std::uint32_t index);
    int AnimationMoment(std::uint32_t index);
    // The step that changes the movement mode `from` into `to` (only heroes, or a change involving Stop).
    bool BuildTransition(struct AnimationSteps* steps, const MoveModeState* from, const MoveModeState* to);
    // For an Attack `source` is the attack key from BuildAttackSteps. Uses are framed by stepping out of and
    // back into the movement mode; *duration = the ticks until the action happens.
    void BuildOrderSteps(struct AnimationSteps* steps, int* duration, UnitOrderKind kind, std::uint32_t source);
    // Draws a random number for heroes.
    void BuildCastSteps(struct AnimationSteps* steps, int* duration, UnitOrderKind kind, int value);
    // The hit location (plan->hitLocation) and the height of the blow from both units' heights and the
    // distance. Draws random numbers.
    void BuildAttackSteps(struct OrderPlan* plan, struct AnimationSteps* steps, int* duration, MapObject* target);

    // Every tick: takes over the queued order, else follows the path and continues the current order
    // (ContinueAction for actions; a finished move or animation lets the queue on).
    void UpdateOrders();
    bool StartQueuedOrder();
    // The Move order: plans a path with the units near the goal lifted off the sector map; done orders let the
    // next one start.
    bool StartMove();
    // Continues an action order while its target is valid and seen, else ends it (party messages NoWayToAttack /
    // NoPath / NoTarget).
    void ContinueAction();
    // When the action can start, rolls an attack's hit and starts it, else closes in (ApproachTarget) or
    // drops the order.
    void ExecuteOrder();
    // Waits when in reach, else plans a path to where the target will be.
    void ApproachTarget();
    // moveMode / moveMode2 from the unit's modes and the order.
    void SyncMoveModes();
    void SetUnitMoveMode2(MoveMode mode);

    // --- The update and the animation queue.
    // Once per world update. A living unit runs its regeneration, AI, item spells, orders, state and animation;
    // a dead one sinks to the ground and, once its death animation is over, reports its death and drops its
    // orders. By state: Idle and Moving move along the path, Turning and Emote react to a hit, Animating waits for
    // the rest of an action's animation and then stops, Acting runs TickAction.
    void UpdateState();
    // Moves the queue on to the world's time and shows its current step on the unit; frozen or reacting units
    // keep their times shifted.
    void AdvanceAnimation();
    // Drops the steps that are over (a pause after one takes its animation over); true when one was dropped.
    bool PopFinishedSteps();
    void ShowCurrentStep();
    void PlaySteps(const AnimationSteps* steps);
    // Starts the queue's time (queue.extra) at the world's tick when it has none (-1).
    void StartQueueIfUnset();
    // Removes the effect object from the world and releases it.
    void DropEffectObject();
    // Other than Acting the size class is updated and the effect object dropped; Acting sets actingCheckTime to
    // 10 updates from now.
    void SetState(UnitState newState);
    // Clears the standing flag, re-registering the unit on the sector map.
    void ClearStanding();
    // Stand: the idle animation (restarted at a random phase when nothing plays), StandStill, then Turning.
    void StandIdle();
    // The unit stays where it is, facing its direction; standing 0.
    void StandStill();
    // The reaction to a hit from hitDirection (BuildDeathAnimation without dying), saving the queue in
    // savedQueue, unless the unit is idle or the reaction would delay the next attack.
    void PlayHitReaction();
    // Aborted (or not started, or with an open-ended animation) the state becomes Stopped, else Animating with
    // what is left of a three-step plan played; the plan is reset and the unit stands still.
    void EndOrder(char abort);
    // State Acting: starts the planned action at its time and finishes it at the end (FinishAction).
    void TickAction();
    // Casting allowed, the target (or a spell's point) not dead and within the plan's range at its end time.
    bool ActionInReach();
    // Each armour spell casts on the unit when its countdown runs out and the item's charge covers it (Healing:
    // one a tick, when hurt).
    void TickItemSpells();

    // False while an attack turns (Turning) and `ticks` from now would pass nextAttackTime.
    bool CanReactFor(int ticks);
    // Play `steps` as an animation order (Animating) / an emote (Emote): the queue takes a copy, the unit stops
    // facing its direction.
    void PlayAnimationOrder(const AnimationSteps& steps);
    void PlayEmoteOrder(const AnimationSteps& steps);
    // The idle animation for the weapon and movement key, rebuilt when the key changed (the emote 0xFE for a hero
    // walking, else the idle entry of the key | kAnimationSpecialIdleKey, if any).
    AnimationSteps* IdleAnimation();
    // Unless idle, moving or turning: the idle animation, rebuilt when needed, plays.
    void PlayIdle();
    // The movement animation of the current mode: looped (an emote of the entry; a hero's by stamina) or `once`
    // (the entry's duration); every step's kind is 2.
    void BuildMoveSteps(AnimationSteps* steps, char once);
    // For a hero the start once, then the loop.
    AnimationSteps* NextAnimation();
    // State Acting: the steps get `missed` (1 when the attack's hit misses) as their flags; the motion turns to the
    // plan's direction or follows its target (until its end when `follow`).
    void StartAction(char follow, char missed);
    // moveMode's run (wasMode3) while moveMode2 asks for it: taken on a cheap cell when not out of breath
    // (and, without a player's permission, with stamina); dropped after 5 updates otherwise; out of breath
    // drops moveMode2's too (remark).
    void UpdateSneak();
    // moveMode2 becomes moveMode unless the unit no longer fits its cell (AckKind::CantChangePosition); a transition animation
    // plays (not while attacking). True when the mode changed.
    bool ChangeMoveMode();
    // FindPath within `range`: no path stands the unit still (Stopped, stoodBlocked from the path's failed
    // flag); a path makes it Idle with its movement animation going on.
    bool PathTo(const Vector3& point, float range);
    // The units lifted off the sector map for a path: away from `point`, those near it
    // outside the player's group; the party members that move (with `moving`) or follow this unit. Only for a
    // player's units with sessionKind 1.
    std::vector<Unit*> LiftNearby(const Vector3& point, char moving);
    // AckKind::NoPath / Move to a party unit's player, unless the brain is following.
    void ReportPath(AIPath* path, char partial);
    // Every field back to its start value (Create, ClearState, Destroy: the manager reuses its objects).
    void ResetController();
    // Drops the orders and takes the movement records from the unit's modes; the wanted weapon is taken up
    // and, unless turning, the idle animation plays (a random phase on an empty queue).
    void ResyncWithUnit();
    // A tame attempt by `tamer` with `roll`: above 10 x the taming virtual plus the taming skill (stats value
    // 2) it moves the unit a stage along (1 not hostile to the tamer's side, 2 on that side, 3 in the tamer's
    // party with a tamed mode); a unit with a player or a failed roll is attacked instead (taming by that side
    // undone). 0/1.
    bool TryTame(Unit* tamer, float roll);
    // The tamed mode (8) removed, no player, all relations, the old side back (from stage 2), stage 0.
    void UndoTaming();
    // From the items the unit wears (weapons only the one in use) whose spell is periodic: kept entries keep
    // their countdown, new ones start at 0, entries of items no longer worn go.
    void RefreshItemSpells();

    // --- The way to an order's target.
    // For a use order on a target (Use, UseSource::Use): the reachable spot the target offers (for the unit's
    // size) whose path is cheapest becomes the order's point (pointIsUseSpot set), its path into `motionOrder` (if
    // given); false when no spot can be reached.
    bool FindUseSpot(UnitAction* order, PathOrder* motionOrder);
    // Simulates the unit along `motionOrder` (a path order, kind 2) in steps of 5 ticks until the order's
    // target is in reach (and seen, when the order needs a line of sight); 0 for other kinds.
    float TimeUntilAction(const AIOrder* order, PathOrder* motionOrder);
    // The use spot, the path (target lifted off the sector map) and whether the unit can act from its end,
    // with the player's path message; the unit shouts `shout` when it can, NoWayToAttack / NoPath when not. Units without
    // a player plan only use orders.
    bool PlanOrderRoute(UnitAction* order, AckKind shout);
    // A party unit's move: the units in the way lifted (LiftNearby), a runner planning as walking where it can,
    // FindPath without a range limit, the units put back and the result reported.
    void PlanMove(const Vector3& point, int moving, int run);

    // `claimant` claims the players in `mask` are enemies of this unit: they join claimedHostile and the AI
    // memory hears a call of kind 0x1E carrying the mask.
    void ClaimHostility(Unit* claimant, std::uint32_t mask);
    // The weapon in `slot` (-1, none, when the slot is empty or the weapon's condition rounds to 0 or less)
    // becomes the active and wanted weapon (part 6 changed); a party unit's AI options are rebuilt, the armour
    // spells refreshed. 1 when the weapon was taken up.
    bool TakeUpWeapon(int slot);
    // Orders aimed at `object` end (the current one reset, then Idle when the queued one is empty; a queued one
    // is emptied unless an animation), a plan aimed at it ends (EndOrder(0)), the brain forgets it and the
    // leader followed is dropped when it is that object.
    void OnObjectDeleted(EngineObject* object);
    // A living unit is set up again where it stands: off the sector map, its figure, size classes and size
    // class; when it cannot leave its cell it moves to a free cell near it, or with none it takes a default hit
    // (a unit that cannot stand dies); then back on the sector map.
    void Resettle() override;
    int TamingStage() override;

    // The death (or fall) animation for a blow from `direction` (HitDirection's bits): `dying` adds the death
    // bits, `fall` (lying down) stops the steps with kind 2 and may pick a side at random. Only the low bytes of
    // `dying` and `fall` count.
    void BuildDeathAnimation(struct AnimationSteps* steps, BlowSide direction, int dying, int fall);

    std::uint8_t frozen = 0;        // the unit is frozen: the animation and the orders wait (UpdateState)
    std::uint8_t animationHeld = 0; // a hit reaction plays (or no size classes): the times shift
    AnimationSteps queue;           // extra: when its first step started, -1 until then
    AIPath motion;
    OrderPlan currentPlan;
    DamageInfo pendingHit{.multiplier = 1.0f};   // the hit of the attack in progress
    std::uint8_t keepHitDirection = 0;   // while set, OnDamaged leaves hitDirection alone
    std::uint8_t stoodBlocked = 0;  // the unit stood still because its path search failed
    std::uint8_t motionChanged = 0; // set when the movement speeds or mode changed (cleared by PathTo)
    std::uint8_t movingAnimation = 0;   // the movement animation is in the queue (cleared when no
                                    // path is found)
    AnimationSteps savedQueue;      // the queue a hit reaction saved (its extra is its time)
    static constexpr BlowSide kNoHitDirection = static_cast<BlowSide>(0xFFFFFFFFu);   // hitDirection: no hit to react to
    BlowSide hitDirection = kNoHitDirection;   // HitDirection of the last hit that did not kill, or kNoHitDirection
    UnitState state = UnitState::Stopped;
    MoveModeState moveMode{MoveMode::Walk, 0, 0};
    MoveModeState moveMode2{MoveMode::Walk, 0, 0};
    int sneakWait = 0;              // updates run was kept without a cheap cell (UpdateSneak)
    Ref<EngineObject> effectObject;   // removed from the world on death
    int actingCheckTime = -1;       // world tick + 10 when acting starts (StartAction), -1 reset
    int nextAttackTime = 0;         // world tick of the next attack
    int interceptTime = 0;          // the tick a chase aims at (the target's position then); saved
    std::uint8_t orderStopped = 0;  // set when a move order ends, cleared when one starts; saved; the AI
                                    // brain reports its target only while it is set (unit_ai.cpp)
    std::uint8_t deathReported = 0; // the death was reported to the world (the dead unit's update)
    std::uint8_t searchingTarget = 0;   // TargetBusy counts a search under way when set (or while Acting)
    UnitAction action;
    UnitAction action2;
    std::uint8_t followsLeader = 0; // the chase follows `followedLeader`'s target (combat_tick.cpp)
    Ref<Unit> followedLeader;      // the party unit whose attack this one joins
    UnitStats stats;                // its flags get UnitStatsFlag::Struck on a hit
    DamageWords protection{};       // [0] the defence value, [1..7] the body's protection
                                    // per damage type (UnitStats::PackBodyProtection)
    float sinkSpeed = 0.0f;         // a dead unit's body sinks this much a tick (accelerating)
    lib::EnumArray<float, MoveFactor, 4> moveFactors = {1.0f, 1.0f, 1.0f, 1.0f};    // UpdateMoveFactors
    int tameStage = 0;              // 0..3 (TryTame)
    int ownSide = -1;               // the unit's own side while tamed (TryTame)
    int tamerSide = -1;             // the tamer's side (TryTame)
    int speedClass = 15;
    std::vector<ArmourSpell> armourSpells;   // periodic spells of the worn items
    int boredTicks = 0;             // a party unit's updates since its last order (a bored remark past a
                                    // limit); cleared when an order starts and by the move style /
                                    // wanted weapon setters
    int waitFrame = 0;              // a frame number; the movement tick waits for a party member
                                    // it blocks while the frame is below this + 2
    static constexpr std::uint32_t kNoAnimationKey = 0x666DEAD;   // moveAnimationKey / idleKey: none built yet
    std::uint32_t moveAnimationKey = kNoAnimationKey;
    AnimationSteps moveAnimation;   // the movement animation (NextAnimation)
    std::uint32_t idleKey = kNoAnimationKey;
    AnimationSteps idleAnimation;   // extra: its time
    void (*actionDone)(Unit*) = nullptr;   // run when Animating ends
    std::uint32_t claimedHostile = 0;   // players the unit has claimed as enemies
    AIBrain brain;
    AIAlarm voice;                  // the voice / alarm
};



