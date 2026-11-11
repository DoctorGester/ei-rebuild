# Combat and the server unit controller

Code (layouts and per-function comments are in `combat.h` and `combat/combat_internal.h`): `combat*.cpp` (queries and
the hit in combat.cpp; actions, orders, order setters, save, sight, steps, step lists, tick, update, motion, planning,
taming, stat slots, lifetime, spell sight), `unit_motion_tick.cpp`, `world_create.cpp`. Scenarios: `combat.txt`,
`combat_more.txt` (kill, archer, heal), `combat_skills.txt` (wand, steal, run and paused move orders), `movement.txt`,
`unit_ai.txt`; every creature runs the update and animation code each tick.

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

Lifetime: the fields carry their start values as initialisers. The server-specific manager reuses destroyed objects
from its spares, so Create, ClearState and Destroy put the fields back with `ResetController` (sneakWait, protection
and waitFrame keep theirs); the manager destroys every object before deleting it, so the destructor has nothing to do.
`OrderPlan::Reset` keeps the plan's direction (it is saved).

## Animation keys

The layout and AnimationKind / BlowSide / SkillAnimation are in objects/object3d.h (names from database.res's *.adb):
weapon bit (1 << type, natural below 7) | movement bits (0x30000 / 0x20000 / 0x10000 / 0x8000) | action bits (0x90000
attack with height band 0x1000000 / 0x2000000 / 0x3000000 | 0x800000, 0x40000 skill, 0xC0000 cast, 0x240000 mode
transition, 0x6000000 death, 0x80580000 idle ...); movement | 0x40000000 once or 0x80000000 looped. An entry's frame
count and the action's moment come from the figure's animation database.

An unarmed unit (no weapon in hand, natural weapon none) asks for a body search with the key bits 0x20 (the bow's bit,
as the original's key builder), and a crossbow's holder asks for the ranged search with 0x40. The heroes' figures
(unhuma's 179 entries, the same in vanilla and Lost in Astral) have the search (0x35, walking 0x30) for 0x5F (the melee
weapons and the crossbow) and the ranged search (0x16, walking 0x34) for 0x20 only, so in the original both searched
with no animation steps. Deviation by the user's choice: a use order whose key fits no entry takes any entry of its
skill (`FindAnimation(key, 1)`, one draw of the game's rand() as every such pick), as an attack falls back to any
attack of its weapon: the unarmed play the melee weapons' search (uspecial24, walking uspecial22), the crossbow the
bow's (uspecial07 / uspecial23). Use, steal and tame entries are for any weapon (0) and unchanged; a figure with no
entry of the skill still plays none (`mod_lost_in_astral_unarmed_search`).

## The hit

PrepareAttack decides backstab (attacker behind; not ranged, not against "cannot be backstabbed") and calls
BuildAttackHit: damage weights; the attack roll (text-key units with a weapon draw ISAAC: 2 * (draw % 5) + 2 - 6,
others the stats' attack times the difficulty factor); damage (the weapon's RollDamage, or damage +
random % (spread * 100 + 1) * 0.01, scaled); the hit location (0 head, 1 body, 2 / 3 hands, 4 / 5 legs, 6 all); the
backstab multiplier; called shot. The target's TakeHit resolves it unless the roll is already +-FLT_MAX: ResolveHit
turns it into FLT_MAX (hit) or -FLT_MAX (miss): elemental hits always hit, else AttackRoll = max(0, 2r - (spread + 1) +
attack - distance penalty + speed ratio (not for a backstab)), r uniform in 0..round(spread), minus 25 for a called
shot at the head or 10 at a limb; the hit lands unless the defender's defence (0 against a backstab) is greater. TakeHit
logs ("Unit 12 was attacked by 34 skill=... damage=... destHL=Body", plus " backstab with power=" / " called shot";
combat.txt `expect`s parts of it, through the console stream, character for character), refuses it for protected players, applies
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
  way: 0.1..0.9 of the way along it and less than 0.3 metres to the side, not a bug; line of sight from the eye height).
  When it plans: the attack interval advanced, the hit built and resolved, the action started; else ApproachTarget
  closes in (wait when in reach, else a path to where the target will be in 2..64 ticks).
- The animation code calls FinishAction(location) at the order's moment: kind 3 DeliverAttack (bows / crossbows: a
  missile 0x5B carrying the hit, a timed object; otherwise the target's ReceiveHit forwards the hit to its TakeHit at
  once; then the weapon's spell and a noise when the backstab multiplier is exactly 1.0), 4 / 5 FinishCast (a charged carrier
  pays from its charge, a party unit pays mana Period20 x cost), 6 UseSkill, 10 the quick item is applied by the world.
  Item spells fire when the item's charge covers the cost (NaN fires); UseItemCharge copies the charge to the party's
  item copy.
- Planning a route: a use order goes to the reachable spot its target offers; otherwise the path is searched with the
  target lifted off the sector map, turned into a motion order, and TimeUntilAction (motion simulated in steps of 5
  ticks) goes to the player's path message. Shouts `NoWayToAttack` (attacks) / `NoPath` on failure.
- Save: SaveServer / LoadServer; the armour spells and the step lists (AnimationSteps::Save / Load and the extra dword
  after them) in `combat_save.cpp`.

## Update, movement tick, damage and death

- Update: UnitVitals::Regenerate, AI events every (speed class + 1)-th frame, armour periodic spells, a party unit's
  perception memory, boredom (past 500 a remark 0x29 is due 1 in 1001 and sets 400, or 0 when the player's flag is
  set; "Facts behind fixed entries" below), alarm, AIBrain::Think, PropagateHostility, then UpdateOrders unless frozen (flags 0x1000000) or reacting to a
  hit, UpdateState and AdvanceAnimation. A dead unit's body sinks (z -= speed, growing each update) and once the queue is
  empty death is reported once.
- **Movement tick** (`UpdateMotion`, called twice by UpdateState; the AI side in ai.md, "How a unit moves"): samples the
  motion order at the world time, starts the next animation, plays a pending fall / death animation; a blocked step
  (BlockingUnitAt) waits (a party member leading this unit, or the unit already waited for), waits behind a unit
  attacking / casting at the same target, or replans with nearby units lifted (links leading back clear both; a slower
  blocker with reason 2, water, also replans); else SectorMap::Move. Running party units drain "stamina" (the server
  object's mana by a fraction of its maximum; below 0: message 0x1D, walk). The "blocked" reason starts at 0 when no
  cell was reached (the original read stack garbage there).
- OnDamaged(attacker, hitKind): types outside 0x32..0x36 only get flags bit 1. Living: armour spells, attacker hostile
  and remembered, flags 0x80000, PropagateHostility (allied units within the brain's radius exchange claims), the hit
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

The original's branch-free selects keep their NaN choices (lib/float_select.h, float-maths.md): `max(0, v)` picks v
when 0 < v or NaN (also the melee reach floor in ActionDistance); a test that skips on equal or unordered is
`x < 0 || x > 0`; random draws `random % (n + 1)` take n rounded to an int, then the zero-extended remainder.

## Unreached in the shipped game

Kept because they could be re-enabled, each with a one-line comment: taming (`TryTame`, `UndoTaming`, `UndoIfTamedBy`
in combat_taming.cpp, `OrderTame`: the taming skill returns before its use case, `WorldServer::CastSpell` refuses
charm and the interface sends only the use and steal kinds) and the use-item order (`OrderPlan::TakeSteps` and the
UseItem cases of `FinishAction` and the tick: nothing sets that order). `TooHighToReach` and the two
`TellParty(OutOfReach)` calls are live code that needs a target too high to reach, which no scenario has.
`combat_skills` reaches the rest of the order code: a wand on a unit (SpellCarrier, UseItemCharge), the science button
on a unit outside the party (the steal order: OrderSteal, UseSkill, "Item has been stolen from"; the hero needs more
science than the wolf's 77) and move orders given while the game is paused (Space: OrderMove plans at once, PlanMove;
with a run flag too).

## Original bugs kept

Visible behaviour of the original kept until the user decides (rewrite-guide rule 9). Crashes, undefined behaviour and
invisible bookkeeping are fixed, not listed.

- Hit: ResolveHit penalises every called-shot location except the body by 10 (including 6 and invalid); CanCast
  compares mana only for units with a player.
- Orders: OrderCastAt's at-once test names wands but only lets potions through; WitnessMurder takes the diplomacy table
  from the unit's manager and the one it changes from the controller's; SetMoveStyle sends message 9 when the style fits
  (even the current one); StopAction always ends with the queued order of kind 0.
- Casting glow: FillCastKind gives RickMagic (school fire) cast kind 1 and CurseMagic (school lightning) kind 0, so the
  caster shows CastingElectricity for the fire arrow and CastingFire for the lightning; every other spell takes its
  prototype's school, as the original (PlanAction's 0x28 -> 1, 0x29 -> 0). A fix would swap the two glows.

Facts behind fixed entries: a natural attack whose damage weights are all zero has no damage type (kNoDamageType: the
panels show none). A move point's z is no height (PlayerMode keeps a flag there, -1313 marks "no point"), so StartMove's
arrival and LiftNearby's "away" tests are in the plane, and PlanMove searches without a range limit. The bored remark
is due by a hash of the tick and the unit's id (1 in 1001): a voice only, so no game draw, and not the client generator,
which would make the saved boredTicks follow the frames.

By design (the user's decision): UseSkill returns for skill 2 (tame) before its switch, so the tame order does nothing
(taming was cut from the original; TryTame is reached nowhere else; "Unreached in the shipped game" above) and
OrderTame compares the target's prototype field (not type) with 0x32.

## Risks

- Scenario hazards: spawn units come from database.res without every variant run (GoblinF1 may beat the starting hero;
  the archer "Human Gipath Brigand A1" may walk up instead of shooting; a wrong prototype name fails `waitfor IsAlive`).
  An order given ~50 ms after AddUnitToServer / SetPlayer is sometimes dropped (about 1 run in 30 with 16 games in
  parallel): scenarios wait 1 s after the spawn, give the order, wait 1 s and give it again.
