# Combat and the server unit controller

Code (layouts and per-function comments are in `combat.h` and `combat_plan.h`): `combat*.cpp` (queries and the hit in
combat.cpp; actions, orders, order setters, save, sight, steps, step lists, tick, update, motion, planning, taming, stat
slots, lifetime, spell sight), `unit_motion_tick.cpp`, `world_create.cpp`. Scenarios: `combat.txt`, `combat_more.txt`
(kill, archer, heal), `movement.txt`, `unit_ai.txt`; every creature runs the update and animation code each tick.

`UnitServer` (class 0x3E) is the server-side unit controller; the AI brain (unit_ai.md) and the path (ai.md) hang off
it.

## Controller fields

Those `combat.h` does not explain: `queue` (AnimationSteps: a `std::vector<AnimationStep>` and `extra`; the time the
first step started, -1 until started); `motion` (AIPath; its `failed` byte is compared with 1); a pending step list; the
plan (OrderPlan); a hit reaction flag and `savedQueue`; the state (0 idle, 1 moving, 2 turning, 3 rest of an action's
animation, 4 as 2 for hit reactions, 5 acting, 6 what an order leaves, "chasing"); two {mode, onGuard, wasMode3} move
records (the second's onGuard: attacking or an AI suspicion / aggression / fear mode; also "cannot be backstabbed"); the
next attack's time; death reported; the current / queued UnitAction (kinds 1 move, 2 turn, 3 attack, 4 / 5 cast spell /
quick item, 6 use or skill (source 0x18A95 talk, 0 science, 1 stealing, 2 taming), 7 play animation, 8 emote, 9 none,
10 use quick item, 11 stop; source = spell slot or quick item slot | 0x80000000); the "waiting for" link
(followsLeader / followedLeader); a UnitStats copy (stats.loadFactor is the hero's "stamina" for walk / run animations:
below two thresholds the movement animation gets the limp bits 0x800000 / 0x400000); sinking speed; movement factors
and speed class; taming stage, own side, tamer's side; the AI-events presence record; `armourSpells` (a vector);
boredom counter; movement and idle animation keys / lists (idle 0x666DEAD: no key yet); `actionDone`, the callback run
when state 3 ends; `voice`. A step is {animation, 0xFF, 0, 0, duration, kind}; duration = entry frames over the unit's
speed factor for it, truncated (a default step: 0xFF, 0xFF, 0, 0, -1, 0).

## Animation keys

The layout and AnimationKind / BlowSide / SkillAnimation are in map_object_figures.h (names from database.res's *.adb):
weapon bit (1 << type, natural below 7) | movement bits (0x30000 / 0x20000 / 0x10000 / 0x8000) | action bits (0x90000
attack with height band 0x1000000 / 0x2000000 / 0x3000000 | 0x800000, 0x40000 skill, 0xC0000 cast, 0x240000 mode
transition, 0x6000000 death, 0x80580000 idle ...); movement | 0x40000000 once or 0x80000000 looped. An entry's frame
count and the action's moment come from the figure's animation database.

## The hit

PrepareAttack decides backstab (attacker behind; not ranged, not against "cannot be backstabbed") and calls
BuildAttackHit: damage weights; the attack roll (text-key units with a weapon use the CRT `rand()`: 2 * ((rand * 5 /
32767) % 5) + 2 - 6, others the stats' attack times the difficulty factor); damage (the weapon's RollDamage, or damage +
random % (spread * 100 + 1) * 0.01, scaled); the hit location (0 head, 1 body, 2 / 3 hands, 4 / 5 legs, 6 all); the
backstab multiplier; called shot. The target's TakeHit resolves it unless the roll is already +-FLT_MAX: ResolveHit turns
it into FLT_MAX (hit) or -FLT_MAX (miss): elemental hits always hit, else AttackRoll = max(0, 2r - (spread + 1) + attack
- distance penalty + speed ratio (not for a backstab)), r uniform in 0..round(spread), minus 25 for a called shot at the
head or 10 at a limb; the hit lands unless the defender's defence (0 against a backstab) is greater. TakeHit logs ("Unit
12 was attacked by 34 skill=... damage=... destHL=Body", plus " backstab with power=" / " called shot"; combat.txt
`expect`s parts of it, through the console stream, character for character), refuses it for protected players, applies
it (UnitStats::ApplyHit, units.md), records the source, recomputes stats and calls OnDamaged. A miss makes the victim
hostile (Diplomacy::OnAttack) and remembered. AttackParams: text-key units use the weapon's stats plus the weapon skill
bonus, others their stats scaled by difficulty; no defence with a bow or crossbow.

## Order lifecycle

- Setters replace the queued order after resetting it (kind 9, target released): attack (reach test, message 0x0C when
  out of reach, then the planner), cast (a scroll is cast at once and used up; a party unit's own spell needs its school
  skill, message 0x13 otherwise).
- Every tick UpdateOrders: StartQueuedOrder takes the queued order over by kind (0 stand, 1 move with a path planned
  around lifted nearby units, 2 turn, 3-6 ExecuteOrder, 7 / 8 animation, 10 use, 11 stop); else action orders go
  ContinueAction (target alive and seen) -> ExecuteOrder -> PlanAction (steps through the ticks of a half turn plus 3 for
  the first moment the target (predicted for moving targets and missile spells) is in reach, the unit can turn in time
  and CanActFrom holds: AI units only within 1.05 times sight range, melee / skill orders not with a living unit in the
  way, line of sight from the eye height). When it plans: the attack interval advanced, the hit built and resolved, the
  action started; else ApproachTarget closes in (wait when in reach, else a path to where the target will be in 2..64
  ticks).
- The animation code calls FinishAction(location) at the order's moment: kind 3 DeliverAttack (bows / crossbows: a
  missile 0x5B carrying the hit, a timed object; otherwise the target's ReceiveHit forwards the hit to its TakeHit at once;
  then the weapon's spell and a noise when the backstab multiplier is exactly 1.0), 4 / 5 FinishCast (a charged carrier
  pays from its charge, a party unit pays mana Period20 x cost), 6 UseSkill, 10 the quick item is applied by the world.
  Item spells fire when the item's charge covers the cost (NaN fires); UseItemCharge copies the charge to the party's
  item copy.
- Planning a route: a use order goes to the reachable spot its target offers; otherwise the path is searched with the
  target lifted off the sector map, turned into a motion order, and TimeUntilAction (motion simulated in steps of 5 ticks)
  goes to the player's path message. Shouts `NoWayToAttack` (attacks) / `NoPath` on failure.
- Save: SaveServer / LoadServer; the armour spells and the step lists (AnimationSteps::Save / Load and the extra dword
  after them) in `combat_save.cpp`.

## Update, movement tick, damage and death

- Update: UnitVitals::Regenerate, AI events every (speed class + 1)-th frame, armour periodic spells, a party unit's
  perception memory, boredom (past 500 one ISAAC draw in 1001 sends remark 0x29 and sets 400, or 0 when the player's flag
  is set), alarm, AIBrain::Think, PropagateHostility, then UpdateOrders unless frozen (flags 0x1000000) or reacting to a
  hit, UpdateState and AdvanceAnimation. A dead unit's body sinks (z -= speed, growing each update) and once the queue is
  empty death is reported once.
- **Movement tick** (`UpdateMotion`, called twice by UpdateState; the AI side in ai.md, "How a unit moves"): samples the
  motion order at the world time, starts the next animation, plays a pending fall / death animation; a blocked step
  (BlockingUnitAt) waits (a party member leading this unit, or the unit already waited for), waits behind a unit
  attacking / casting at the same target, or replans with nearby units lifted (links leading back clear both; a slower
  blocker with reason 2, water, also replans); else SectorMap::Move. Running party units drain "stamina" (the server
  object's mana by a fraction of its maximum; below 0: message 0x1D, walk). The "blocked" reason starts at 0 when no cell
  was reached (the original read stack garbage there; no golden reaches it).
- OnDamaged(attacker, hitKind): types outside 0x32..0x36 only get flags bit 1. Living: armour spells, attacker hostile and
  remembered, flags 0x80000, PropagateHostility (allied units within the brain's radius exchange claims), the hit
  direction. Dead: the killer's party gets message 0x30, WitnessMurder on both nearby lists, effects cleared, SetDead(1),
  "Unit 12 damaged to death", death animation, AI death notice, party memory. HitDirection: |side| < |front| (or NaN)
  gives 0x800000 front / 0x400000 behind, else 0xC00000 / 0x1000000.
- UseSkill (kind 6): talk (0x18A95) starts the dialog; stealing (1) fails unless the thief's skill is greater, else takes
  a random item (message 0x22 on failure); science (0) raises the item record's third int. Taming (`TryTame`,
  `UndoTaming`): a roll succeeds when above 10 x the taming stage plus the taming skill or NaN; the tamed mode is a
  player mode.

## World managers

`World::CreateManagers` (client and server) makes and references the world's managers (players, map objects, figures,
effects, spells, items, resources), takes the command-line paths, then initialises ISAAC and the water levels.
`World::Destroy`: spell objects get ClearState, managers deleted whatever their counts. Player sources
(`player_source.h`): `WorldHandle` (a counted pending link), `netlink::DriverSession` (World::OpenSession).

## Floating point

The original's branch-free `max(0, v)` picks v when 0 < v or NaN (`MaxZero`; also the melee reach floor in
ActionDistance); `TEST AH,0x40` skips on equal or unordered (`x < 0 || x > 0`); random draws `random % (n + 1)` with n =
FISTP, then the zero-extended remainder (`Unsigned`).

## Original bugs kept

- Hit: the melee reach floor for two party units is 0x3E4CCCCE (0.20000002); GetAttackParams keeps the damage type when
  the natural attack has no non-zero weight; BuildAttackHit's weapon branch rolls with `rand()`, the rest with ISAAC;
  TakeHit logs " skill=" with the resolved roll (always +-FLT_MAX by then); ResolveHit penalises every called-shot location
  except the body by 10 (including 6 and invalid); CanCast compares mana only for units with a player.
- Planning and steps: LoadServer writes a field for version 0x73+ instead of reading it, Save writes constant 2 after
  another; BuildOrderSteps uses the step list's address as the source of an unknown skill, BuildAttackSteps reads the
  plan's address as a float for an invalid location; CanActFrom's "in the way" test divides by the squared distance;
  StartMove passes the goal's z as the path range; ApproachTarget returns with the path flag cleared when the goal is
  under 0.1 away.
- Orders and skills: FinishAction's kind 10 and Idle reset the queued order to 9 and then 0; UseSkill returns for skill 2
  before its switch (taming there is dead), dereferences the target without a null check outside talk, calls OnAttack
  with a null victim for a non-unit, logs "Unit" without a space; OrderMove stores its fourth argument over the point's z;
  OrderCast ignores the aim point it asks for; OrderCastAt checks category 5 or 8 then 8; WitnessMurder takes the
  diplomacy table from the unit's manager and the one it changes from the controller's; PropagateHostility compares
  squared distance and radius as ints; SetMoveStyle sends message 9 when the style fits (even the current one);
  OrderTame compares the target's prototype field (not type) with 0x32; OrderSearchBody wants a dead target;
  PlayAnimation reads the entry's index byte as a dword; StopAction always ends with the queued order of kind 0.
- Update and animation: ActionInReach: a cast whose target is gone but whose spell has flag 0x20000000 reads the null
  target's position (access violation in both); PlanOrder looks up the spell of the current action, not of the planned
  order; FindUseSpot writes z = -1 into every order's point; ResyncWithUnit tests "current order not 0" right after
  setting 9 and "state not 2" after setting 6; RefreshItemSpells drops the spell for a node without an item (the
  original kept the previous item's); PathTo saves and restores a unit flag around FindPath; NextAnimation passes a pause
  step's emote as the misaligned dword at step + 1; LiftNearby's first loop does nothing; boredom draws ISAAC only past
  500.
- LoadArmourSpells' second loop reads an id and countdown for every entry of the list, old ones included.

## Risks

- Scenario hazards: spawn units come from database.res without every variant run (GoblinF1 may beat the starting hero;
  the archer "Human Gipath Brigand A1" may walk up instead of shooting; a wrong prototype name fails `waitfor IsAlive`).
  An order given ~50 ms after AddUnitToServer / SetPlayer is sometimes dropped (about 1 run in 30 with 16 games in
  parallel): scenarios wait 1 s after the spawn, give the order, wait 1 s and give it again.
