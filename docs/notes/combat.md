# Combat and the server unit controller (00540000-00560000)

Code (layouts and per-function comments are in `combat.h` and `combat_plan.h`): `combat*.cpp` (queries and the hit in
combat.cpp; actions, orders, order setters, save, sight, steps, step lists, tick, update, motion, planning, taming, stat
slots, object / server slots, lifetime, spell sight), `unit_motion_tick.cpp`, `world_create.cpp`, `world_managers.cpp`.
Verify: `tests/verify/verify_combat*.cpp`, `verify_unit_motion_tick.cpp`, `verify_world_create.cpp`. Scenarios:
`combat.txt`, `combat_more.txt` (kill, archer, heal), `movement.txt`, `unit_ai.txt`; every creature runs the update and
animation code each tick, so any scenario with units reaches it.

## Address map

Most of the range is **CUnitServer** (class id 0x3E, vtable 0073EF38, 54 slots, GetClassName 005460C0; abstract base vtable
0073EDBC; constructor 00545580, 0xB20 bytes), the server-side unit controller at CUnit + 0x240.

| Range | Contents |
|---|---|
| 00546480-0054B220 | orders and movement: animation queue 00546B40, path following 00547890 / 0054A150 |
| 0054A900, 0054B390 | the per-tick order machine |
| 0054B220-0054CA40 | weapon / spell queries, action distance, sight checks |
| 0054CA40-0054F000 | order execution: attack, cast (0054E600), use, target selection |
| 0054F000-0054F8E0 | skill use; attack execution (missiles 0054F450, hit 0054F580, item spells) |
| 00550050-005518F0 | animation choice by weapon and movement, steps |
| 00551AE0-00552D00 | taming, side changes, hostility claims 00552D00 |
| 00552E00-005543B0 | hostility propagation, stats / state slots (19-29, 44-49), death 00553AC0, TakeHit, heal |
| 00554450-00556760 | order setters; attack parameters and roll; Load 00555D10 / Save 005561C0 |
| 0055C000-00560000 | world managers ("CPlayerManager", "CEffectManager", "CItemManager", maps), World::FindUnit 0055FB00 |

## Facts

### Controller fields (those `combat.h` does not name or explain)

+0x20 frozen this update, +0x21 held by a hit reaction (AdvanceAnimation shifts motion, queue and plan times by one while
set); +0x24 `queue` (AnimationSteps: steps +0x28, count +0x2C, +0x38 time the first step started, -1 until started); +0x3C
`motion` (AIPath, built by AIPath::Construct 006706A0 = the inlined 00545750 x2 / 00557230 / 005B23A0; its `failed` byte is
compared with 1 by MotionOver, Replan and PathTo, so the rewrites read it as a byte); +0x78 pending step list; +0x9C /
+0xA4 point to path to and its range; +0xA9 / +0xAA path flags (+0x6D, +0x6E no player); +0xC4 plan (OrderPlan; +0xF0
location, +0xF4 notSkill); +0x144 a hit reaction plays, +0x148 `savedQueue` (time +0x15C); +0x145 last path blocked, +0x146
pending path, +0x147 moving; +0x164 state (0 idle, 1 moving, 2 turning, 3 rest of an action's animation, 4 as 2 for hit
reactions, 5 acting, 6 what an order leaves, "chasing"); +0x168 / +0x170 {mode, flag4, wasMode3} move records (+0x174 =
flag4 of the second is also "cannot be backstabbed"); +0x178 updates in mode 3 without a cheap cell; +0x180 update at which
the size class is refreshed while acting; +0x184 next attack's time; +0x18D death reported; +0x190 / +0x1B4 current /
queued UnitAction (kinds 1 move, 2 turn, 3 attack, 4 / 5 cast spell / quick item, 6 use or skill (source 0x18A95 talk, 0
science, 1 stealing, 2 taming), 7 play animation, 8 emote, 9 none, 10 use quick item, 11 stop; source = spell slot or quick
item slot | 0x80000000); +0x1D8 / +0x1DC "waiting for" link (followsLeader / followedLeader); +0x1E0 UnitStats copy (0x704;
00552F40 in, 00553360 out; start = the CombatStats overlay; stats.loadFactor (UnitStats +0x58, unit +0x238) is the hero's
"stamina" for walk / run animations: below 007C5448 / 007C5440 the movement animation gets the limp bits 0x800000 /
0x400000); +0x8F4 sinking speed; +0x8F8..+0x904 movement factors, +0x914 speed class (005536B0); +0x908 taming stage,
+0x90C own side, +0x910 tamer's side; +0x918 AI-events presence record (`AIEventPresence*`, x, y at +8 / +0xC); +0x91C
`armourSpells` (CList, vtable 0073F010); +0x938 boredom counter; +0x940 / +0x944 movement animation key / list; +0x95C /
+0x960 / +0x974 idle key / list / time (0x666DEAD: no key yet); +0x978 `actionDone`, the callback run when state 3 ends;
+0x984 brain's mode (6: no path reports); +0xB10 `voice` (005AEA50 zeroes it). The brain's +0xD0 and +0xD8 are both sight
caches {0, -1.0}. Animation keys (005301B0): weapon bit (1 << type, natural below 7) | movement bits (0x30000 / 0x20000 /
0x10000 / 0x8000) | action bits (0x90000 attack with height band 0x1000000 / 0x2000000 / 0x3000000 | 0x800000, 0x40000
skill, 0xC0000 cast, 0x240000 mode transition, 0x6000000 death, 0x80580000 idle ...); movement | 0x40000000 once or
0x80000000 looped. A step is {animation, 0xFF, 0, uninitialised, duration, +8}; duration = entry frames (+0x20; +0x40 the
action's moment) over the unit's speed factor for it (005327D0), truncated. CUnit +0x350 (the last damage source) is
UnitParams::unknown_20.

### The hit

PrepareAttack (0054F580) decides backstab (00530A60: attacker behind; not ranged, not against +0x174) and calls
BuildAttackHit (0054F630) into +0x108: damage weights; +0x20 attack roll (text-key units with a weapon use the CRT
`rand()`: 2 * ((rand * 5 / 32767) % 5) + 2 - 6, others the stats' attack times the difficulty factor 007C54AC[0079BEC0]);
+0x24 damage (the weapon's RollDamage, or damage + random % (spread * 100 + 1) * 0.01, scaled by 00523D30); +0x2C hit
location (0 head, 1 body, 2 / 3 hands, 4 / 5 legs, 6 all); +0x34 backstab multiplier (00525D60); +0x38 called shot. The
target's TakeHit (00553F40, slot 20) resolves it unless +0x20 is already +-FLT_MAX: ResolveHit (00553DF0) turns it into
FLT_MAX (hit) or -FLT_MAX (miss): elemental hits always hit, else AttackRoll (00555BD0) = max(0, 2r - (spread + 1) + attack
- distance penalty + speed ratio (not for a backstab)), r uniform in 0..round(spread), minus 25 for a called shot at the
head or 10 at a limb; the hit lands unless the defender's defence (0 against a backstab) is greater. TakeHit logs ("Unit 12
was attacked by 34 skill=... damage=... destHL=Body", plus " backstab with power=" / " called shot"; combat.txt `expect`s
parts of it), refuses it for protected players, applies it (UnitStats::ApplyHit 005254B0), records the source at CUnit
+0x350, recomputes stats and calls OnDamaged (00553AC0). A miss makes the victim hostile (CDiplomacy::OnAttack) and
remembered. AttackParams (slot 50, 00555A00): text-key units use the weapon's stats plus the weapon skill bonus (00526C70),
others their stats scaled by difficulty (0052E0D0); no defence with a bow or crossbow.

### Order lifecycle

- Setters replace the queued order after resetting it (kind 9, target released): attack 00554B30 (reach test =
  CanReachTarget inlined, message 0x0C when out of reach, then the planner 0054E600), cast 00554D00 / 00554F50 (a scroll is
  cast at once and used up via 00595020; a party unit's own spell needs the spell's +0x50 against UnitStats 00527EB0,
  message 0x13 otherwise).
- Every tick UpdateOrders: StartQueuedOrder takes the queued order over by kind (0 stand, 1 move with a path planned around
  lifted nearby units, 2 turn, 3-6 ExecuteOrder, 7 / 8 animation, 10 use, 11 stop); else action orders go ContinueAction
  (target alive and seen) -> ExecuteOrder -> PlanAction (plan into +0xC4: steps through the ticks of a half turn plus 3 for
  the first moment the target (predicted for moving targets and missile spells) is in reach, the unit can turn in time and
  CanActFrom holds: AI units only within 1.05 times sight range, melee / skill orders not with a living unit in the way, line
  of sight from the eye height). When it plans: attack interval (+0x184) advanced, hit built and resolved, 005494D0 starts
  the action; else ApproachTarget closes in (wait when in reach, else a path to where the target will be in 2..64 ticks).
- The animation code (00549160) calls FinishAction(location) at the order's moment (00549EC0): kind 3 DeliverAttack (bows /
  crossbows: the world's object factory creates missile 0x5B with the 0x3C-byte hit at +0x160, registered with
  AddTimedObject; otherwise the target's CUnit slot 31 forwards the hit to its TakeHit at once; then the weapon's spell and
  a noise of 2 x CombatStats +0xFC when the backstab multiplier is exactly 1.0), 4 / 5 FinishCast (a charged carrier pays
  from its charge, a party unit pays mana Period20 x cost), 6 UseSkill, 10 the quick item of category 2 is applied by the
  world (0058ECE0). Item spells fire when the item's charge covers the cost (NaN fires); UseItemCharge 00552C60 copies the
  charge to the party's item copy (player + 0x70).
- Planning a route (0054E600): a use order goes to the reachable spot its target offers (slot 44 / 60) within 0073EF28;
  otherwise the path is searched with the target lifted off the sector map (path flag 007B00D2 cleared, CUnit +0x43C = 1),
  turned into a motion order (005B22C0), and TimeUntilAction (motion simulated in steps of 5 ticks) goes to the player's
  path message (0066D490). Shouts 0x0B (attacks) / 0x0E on failure.

### Update (slot 6, 00552300), movement tick (00547890), damage and death

- Update: UnitVitals::Regenerate, AI events (world + 0x2838, 005C9770) every (+0x914 + 1)-th frame, armour periodic spells
  (00552B90), a party unit's perception memory (+0xA50, 005CB850), boredom (+0x938; past 500 one ISAAC draw in 1001 sends
  remark 0x29 and sets 400, or 0 when the player's +0x5120 is set), alarm, AIBrain::Think, PropagateHostility, then
  UpdateOrders unless frozen (flags_ 0x1000000) or reacting to a hit, UpdateState (005499F0) and AdvanceAnimation. A dead
  unit's body sinks (z -= +0x8F4, growing by 0073C548 per update) and once the queue is empty death is reported once
  (+0x18D: 00593AF0, 005300D0, brain / alarm resets).
- Movement tick (`UpdateMotion`, called twice by 005499F0; the AI-side chain is in `ai.md` "How a unit moves"): samples the
  motion order (+0x3C) at the world time, starts the next animation, plays a pending fall / death animation; a blocked step
  (BlockingUnitAt) waits (a party member leading this unit, or the unit already waited for), waits behind a unit attacking /
  casting at the same target, or replans with nearby units lifted (links leading back clear both; a slower blocker with
  reason 2, water, also replans); else CSectorMap::Move. Running party units drain "stamina": the server object's
  vitals.mana (+0x14) -= vitals.maxMana (+0x18) * 0073F088; below 0: message 0x1D, walk.
- OnDamaged(attacker, hitKind): types outside 0x32..0x36 only get flags_ bit 1. Living: armour spells, attacker hostile and
  remembered (005D7E90), flags_ 0x80000, PropagateHostility (allied units within the brain's +0x38 radius exchange claims
  through 00552D00), hit direction to +0x160 unless +0x144. Dead: the killer's party gets message 0x30, WitnessMurder on both
  nearby lists (brain + 0xF4), effects cleared, SetDead(1), "Unit 12 damaged to death", death animation (00550A80,
  00548DE0), AI death notice (005C97B0), party memory (005CBE90). HitDirection: |side| < |front| (or NaN) gives 0x800000
  front / 0x400000 behind, else 0xC00000 / 0x1000000.
- UseSkill (0054F000, kind 6; no script command reaches it): talk (0x18A95) starts the dialog (005935C0, Rebirth-patched,
  called through its address); stealing (1) fails unless the thief's skill is greater, else takes a random item (00531400)
  (message 0x22 on failure); science (0) raises the item record's third int. Taming (`TryTame` 00551AE0, `UndoTaming`
  00552280): +0x908 stage; a roll succeeds when above 10 x slot 39 plus the taming skill or NaN; the tamed mode is a player
  mode (005D3760, vtable 0073F0C4).
- World::CreateManagers (0055EE40, from the client init 00561E50 and the server's 0058AE10) makes and references the world's
  managers at +0x38 (0xB0), +0x40 (0xBC, 005125E0), +0x3C (0xD0, 005AA070), +0x48, +0x50 (00683FF0), +0x4C (00688840),
  +0x58 (006A5AB0), +0x54 (CResourceManager 00674D90), copies the command-line paths (0079BC90..98) to 007AF460 / 45C / 458,
  then IsaacRandom::Init and 006D43E0 on +0x70. CWorld::DestroyManagers (0055F8E0, base slot 3): spell objects get slot 5
  (ClearState), managers deleted whatever their counts.

### Floating point and verification patterns

The original's branch-free `max(0, v)` picks v when 0 < v or NaN (`MaxZero`; also the melee reach floor in ActionDistance);
`TEST AH,0x40` skips on equal or unordered (`x < 0 || x > 0`); random draws `random % (n + 1)` with n = FISTP, then FILD of
the zero-extended remainder (`Unsigned`). Random-number wrappers keep their snapshots on the stack so nested wrappers
(AttackRoll in ResolveHit, BuildAttackHit in PrepareAttack, RollDamage) each leave the generator as one original run would.
005536B0 starts with PUSH / MOV and a relative CALL, so its wrapper (`stolenBytes` 0) calls `OriginalUpdateMoveFactors`, a
naked stub in verify_combat.cpp. The movement tick's stale-local quirk is reproduced by reading the stack dword at entry
ESP - 0x98 in a naked entry before the frame exists.

## Rewritten

V: P pure; O output (original into a scratch copy); R random (generators (ISAAC, rand() seed at `_getptd` + 0x14) saved
and restored around both runs, compared too); S stateful (ours, bytes restored, original, compared); C capture streams;
ours = consistency checks (most order / update code: run every tick).

| Address | Name | V |
|---|---|---|
| 0054B620, 0054B650, 0054B680, 0054B6C0, 0054B760, 0054B9C0, 0054B7C0, 0054B820, 0054B880, 0054BF80, 0054C070, 0054C800, 00550050, 005500A0, 00556760 (24), 005518F0, 00551A40, 00551A90, 0054C2F0, 00549D00, 00549D40, 0054C760, 005567A0 (36), 005567E0 (37), 0054BBC0 | AttackRange, WeaponType, HasRangedWeapon, AttacksFromRange, ScaledItemValue, ScaledAttackValue, SpellCarrier, IsScrollSource, SpellOfSource, CanCast, ActionDistance, CanReachTarget, WeaponTypeMask, MoveModeMask, Level, HitDirection, AnimationDuration, AnimationMoment, CanActFrom, CanReactFor, ActionInReach, TargetBusy, HasOtherArmour, HasMainArmour, SpellHasLineOfSight (ECX world, EDX spell; prototype +0x40 needsSight; from 0058E850's multi-target loop) | P |
| 0054B580 | IsFighting (Rebirth-patched: FixPlaingBattleMusicFromAntimagic behind the flag) | |
| 0054BA30, 0054BB20, 0054B520, 00555A00 (50), 0054BCB0 | GetDamageByType, GetSpellDamage, GetMovePoint, GetAttackParams, FindSightTarget | O |
| 00553220 (49), 00552FB0 (23), 005536B0 (45), 0054A620, 005533B0 (19), 00553450, 00553500 (22), 00553560 (26), 005535C0 (27), 005535E0 (28), 00553360 (O) | UpdateSpellBlockFlag, UpdateSizeClasses, UpdateMoveFactors, SyncMoveModes, RefreshVitals, UpdateProtection, RepackStats, AddArmourLayerOf, RebuildArmourLayers, ClearOuterArmourLayers, CopyStats | S |
| 00555BD0, 00553DF0, 0054F630, 0054F580, 0054C6B0, 0054CA40, 005516D0, 00550100, 00550620, 0054F970, 00550A80, 00550D90, 005511A0 | AttackRoll, ResolveHit, BuildAttackHit, PrepareAttack, CanPlanAction, PlanAction (ours into the real plan, the original into a deep copy), BuildTransition, BuildOrderSteps, BuildCastSteps, BuildAttackSteps, BuildDeathAnimation, BuildIdle, BuildMoveSteps (plans compared without byte 3) | R |
| 00553F40 (20) | TakeHit (an unresolved hit comes back +-FLT_MAX; a result of 1 has set bit 0x10 of stats +0x6F8) | ours |
| 00549EC0, 0054F450, 0054F850, 0054F8E0, 00552C60, 005543B0, 00553AC0, 005538D0, 00552E00, 0054F000, 00555180, 00546720, 0054EE10, 005546B0, 00554740, 00554B30, 00554D00, 00554F50, 00555200 | FinishAction, DeliverAttack, TriggerWeaponSpell, TriggerItemSpells, UseItemCharge, Heal, OnDamaged, WitnessMurder, PropagateHostility, UseSkill, Idle, SetDead, FinishCast, OrderTurn / Move / Attack / CastAt / Cast, StopQueued | ours |
| 005561C0 / 00555D10 | SaveServer (10, C) / LoadServer (9) | |
| 0054B230, 0054B320, 00546830, 0054A6E0, 0054A900, 0054B390, 0054DD70, 0054D960, 0054D140, 00552300 | UnitAction::Reset / Assign, SetUnitMoveMode2, TakeQueuedOrder, StartQueuedOrder, UpdateOrders, ContinueAction, ExecuteOrder, ApproachTarget, UpdateServer (6: returns 1; a living unit ends with hitDirection -1) | ours |
| 005499F0, 00546F30, 00546C10, 00546C40, 00547090, 005467B0, 00547280, 00547100, 00549690, 00549160, 00552B90, 005528F0, 00546B40 (S) | UpdateState, AdvanceAnimation, ShowCurrentStep, PlaySteps, SetState, SetStanding, StandStill, PlayHitReaction, EndOrder, TickAction, TickItemSpells, RefreshItemSpells, PopFinishedSteps | ours |
| 00548DE0, 00548FA0, 00550F30, 005474B0, 00551400, 005494D0, 00548520, 0054A150, 00548700, 005468D0, 0054EB30, 00546480, 00554450 | PlayAnimationOrder (state 3), PlayEmoteOrder (4), IdleAnimation, PlayIdle, NextAnimation, StartAction, UpdateSneak, ChangeMoveMode, PathTo, LiftNearby, ReportPath, ResetController, ResyncWithUnit (`combat_motion.cpp`) | ours |
| 0054DF50, 0054E250 (ST0; compared), 0054E600, 00554830, 00551AE0, 00552280, 00547890 | FindUseSpot, TimeUntilAction, PlanOrderRoute, PlanMove, TryTame, UndoTaming, UpdateMotion | ours |
| 0055EE40, 0055F8E0, 00558FA0 (C) | World::CreateManagers, CWorld::DestroyManagers, UnitSetEntryHead::Write (SendZone's unit set entry head; `world_unit_set.cpp`) | ours |
| 0055F2E0 / 0055F2F0, 0055F1A0 (compared), 0055F1E0, 0055F280 / 0055F260, 0055F550 (compared), 0055F6B0, 0055F7B0 / 0055F790, 0055F410 | Player sources (`player_source.h`, `world_sessions.cpp`; A-U8): CPlayerSource base destructor (vtable 0073F38C), CWorldHandle (+0x5C; +8 a counted pending link) Clone / OnShutdown / destructors, netlink::DriverSession (+0x60, 0073F3A8; was "the game result object") Clone / OnShutdown / destructors, CWorld::OpenSession (new session ShareAssigned into +0x60) | ours |
| 0055F310-0055F400 (names / ids P) | CPlayerManager, CEffectManager (0073F2C4), CItemManager class names, ids and destructors (`world_managers.cpp`); 0055F3B0 (CItemManager slot 25 "items") left: CResourceManager does not declare slot 25 | ours |
| 0054A5A0, 00555270, 005552E0, 005553B0, 00555480, 00555570, 00555630, 00555700, 00555840, 00512F60 | FitsMoveMode, SetMoveStyle, SetWantedWeapon, OrderSteal / Tame / Use / SearchBody, PlayAnimation, StopAction (state 6, orders 9 / 0, empty plan), FindFigureAnimation (`combat_order_setters.cpp`) | ours |
| 005535F0 (29), 00552CD0 (38), 00553380, 00552D00, 00553610 (25), 00552670 (42), 00553260 (44), 00546450 (2), 00546080 (5), 005460A0 (39, P) | RefreshEquipment, ForgetClaims, AddExperience, ClaimHostility, TakeUpWeapon, OnObjectDeleted, Resettle, CreateServer, ResetSlot, TameStage | ours |
| 00544CE0, 00544D50, 00544DD0, 00544E40 (P), 00544E70 (P), 00544F60 (C), 00544EF0, 00544FD0, 00545110 (O) | AnimationSteps::AddStep / AddEmote / SetStepFlags (ours on a deep copy, original on the list, compared without byte 3) / Duration / SameAs / Save / Load, OrderPlanReset, OrderPlan::TakeSteps (only for a queued order of kind 10, whose one setter 005557E0 is dead: unreachable) | |
| 00545580, 00545990 (O on raw memory), 00545CB0 (this = brain + 0xD0), 005457E0, 00546100 | CUnitServer::Construct, AIBrain::Construct, AIBrain::DestructMemory, AIBrain::Destruct (only from unwind funclets), AIPath::Destruct (from CPlayerServer::OnUseOrder) (`combat_lifetime.cpp`; every spawn; destructors through 00546230 at zone changes) | |
| 00558420, 00545430 (C), 005582A0, 005585F0 | SaveStepList, SaveArmourSpell, LoadStepList, LoadArmourSpells (`combat_save.cpp`; loads through a recorder, step lists round-tripped through the original save; world_load, ui_saveload) | |
| 00551E10-00552180 (12) | Player / tamed AI mode slots: Clone, Reset (OnShutdown), Destruct, DeletingDestruct, priorities 10 / 100000000, types 6 / 8 (`unit_ai_player_mode_slots.cpp`) | constants P |
| 005460C0, 005460D0, 005460E0, 00546090 (16), 005460B0 (17), 00552F30 (53), 00545F70, 00550A60 | CUnitServer class name / id, DeletingDestruct, MotionSlot, empty slot, slot 53 (UpdateMoveFactors), UnitAction / AnimationStep constructors (`combat_server_slots.cpp`) | P / ours |

## Original bugs and oddities kept on purpose

- Hit: melee reach floor for two party units is 0x3E4CCCCE (0.20000002); GetAttackParams keeps the damage type when the
  natural attack has no non-zero weight; BuildAttackHit's weapon branch rolls with `rand()`, the rest with ISAAC; TakeHit
  logs " skill=" with the resolved +0x20 (always +-FLT_MAX by then); ResolveHit penalises every called-shot location except
  the body (1) by 10 (including 6 and invalid); CanCast compares mana only for units with a player; IsScrollSource calls
  GetCategory up to three times.
- Planning and steps: LoadServer writes +0x93C for version 0x73+ instead of reading it, Save writes constant 2 after +0x180;
  BuildOrderSteps uses the step list's address as the source of an unknown skill, BuildAttackSteps reads the plan's address
  as a float for an invalid location; steps carry an uninitialised byte 3 (ours 0); CanActFrom's "in the way" test divides
  by the squared distance; PlanAction's duration local is uninitialised; StartMove passes the goal's z as the path range;
  ApproachTarget returns with path flag 007B00D2 cleared when the goal is under 0.1 away; ExecuteOrder passes 005494D0
  dwords with garbage upper bytes.
- Orders and skills: FinishAction's kind 10 and Idle reset the queued order to 9 and then 0; UseSkill returns for skill 2
  before its switch (taming there is dead), dereferences the target without a null check outside talk, calls OnAttack with a
  null victim for a non-unit, logs "Unit" without a space; OrderMove stores its fourth argument over the point's z;
  OrderCast ignores the aim point it asks for; OrderCastAt checks category 5 or 8 then 8; WitnessMurder takes the diplomacy
  table from the unit's manager and the one it changes from the controller's; PropagateHostility compares squared distance
  and radius as ints; OnDamaged's death branch asks for ManagerContext and ignores it; SetMoveStyle sends message 9 when the
  style fits (even the current one); OrderTame compares the target's +0x18 (not type_) with 0x32; OrderSearchBody wants a
  dead target; PlayAnimation reads the entry's index byte as a dword; StopAction always ends with the queued order of kind 0.
- Update and animation: UpdateState / PlayHitReaction copy the saved / new animation into the queue twice, PlaySteps resizes
  and copies twice, EndOrder / TickAction assign the new queue twice; ActionInReach: a cast whose target is gone but whose
  spell has flag 0x20000000 reads the null target's position (access violation in both); ReportPath / PlanOrder pass the
  path message a stack motion order whose order part is uninitialised when no path was copied (ours zeroes); PlanOrder looks
  up the spell of `action` (+0x194), not of the planned order; FindUseSpot writes z = -1 into every order's point;
  ResyncWithUnit tests "current order not 0" right after setting 9 and "state not 2" after setting 6; RefreshItemSpells
  keeps the previous item's spell for a node without an item (ours none); 005483C0 copies the extra dword too, 00556950
  only the steps; PathTo saves and restores CUnit +0x43C around FindPath; NextAnimation passes a pause step's emote as the
  misaligned dword at step + 1; LiftNearby's first loop does nothing; boredom draws ISAAC only past 500.
- Movement tick: the "blocked" local is uninitialised when no cell was reached or the cell is outside the AI map
  (CanStandAtFromHere returns before writing the reason), the original decides on stack garbage (ours reads the same stack
  dword); the animation key passed to 00530240 keeps the queue count's upper 24 bits; the fall animation is assigned to the
  queue twice. Taming, CreateManagers and the mode Clones write reference counts / the new mode's +4 before testing the
  allocations (the Clones test first in ours).
- Lifetime: TakeSteps clears isCast twice; LoadArmourSpells' new entries carry a stack byte in `worn` (ours 0) and its
  second loop reads an id and countdown for every entry of the list, old ones included; an unread step-list count is stack
  garbage (ours 0); the controller constructor sizes the movement list with SetSize(0) out of line and the idle list inline. The player
  sources' Clones and OpenSession write the new object's count before testing the allocation (ours test first);
  their OnShutdowns return a leftover EAX (ours 0; RefPtr::Destroy ignores it).

## Skipped and left

- 0055E990: Rebirth-patched. 0054C8B0 / 0054C960: return the FPU status word in AX on one path (callers test AL; always
  called through the address). 00536670 / 00535CB0 / 005381B0 are the client's (units.md). AI forwarders: 005CEED0
  AttentionRebuild, 005CCBF0 CallsDropClaims, 005CBBC0 MemoryAddCall (originals_ai.h).
- Library: containers, CArchive lists (00556E10-00558700), 11 container functions in 0x545000-0x557000 (the AnimationSteps
  copies 0054B260 / 005483C0 / 00556870 / 00556950, list constructors / destructors), 21 MFC template functions in
  0x5576C0-0x559000. EH-only helpers left unported: 005461E0 / 00545FD0 (an order's target release), 00546200, 00556AA0,
  00545B80; 0054B220 is called only by the rewritten 0054A900. Game code left in 0x5576C0-0x559000: 005587A0 (a mode
  reference's destructor), 00558840 (armour spell element assignment), 00558CA0 / 00558CC0 / 00558CE0 (the memory records'
  deleting destructors and a reference reset).

## Risks

- Almost everything is checked for consistency only, so a wrong branch that leaves a consistent state is invisible to the
  wrappers; the scenarios are the behavioural check. TakeHit's log line goes through forwarded ostream code (006FC31D,
  006FC9C6, 006FC7FA, 006FC0B0, 00434FE0; log stream pointer 007AF3E8 = the console ostream at 007C5810) and must match
  character for character.
- The CombatStats overlay names bytes of UnitStats that unit.h leaves unnamed; merge if the units area names them.
  `+0x174`'s naming is a guess (behaviour copied). x87: values kept on the FPU stack are `double` locals (exact at 24 / 53-bit
  precision, not at 64); FinishCast's clamps select through float loads where the original selects dwords with masks (only
  an sNaN mana could differ); OnDamaged keeps the death animation in a local AnimationList the original frees from an EH frame.
- Scenario hazards: spawn units come from database.res without every variant run (GoblinF1 may beat the starting hero; the
  archer "Human Gipath Brigand A1" may walk up instead of shooting; a wrong prototype name fails `waitfor IsAlive`). An order
  given ~50 ms after AddUnitToServer / SetPlayer is sometimes dropped (about 1 run in 30 with 16 games in parallel):
  scenarios wait 1 s after the spawn, give the order, wait 1 s and give it again (`hp0` is read before the first order).

## C6 cleanup (2026-10-09)

Offsets, VCall and byte arithmetic removed (behaviour unchanged) from combat_tick, combat_update, combat, combat_internal.h,
magic_trap, combat_save, unit_skins, unit_lever, unit_client_path, unit_points, combat_taming, combat_planning,
unit_client_models; DR-C3 did unit_motion_tick.cpp, combat_motion.cpp and net_state.cpp. Facts found: map_object.h slot 44
is `UseSpots(void* out, float userRadius)`, slot 59 `double MoveSpeed()` (ST0, unrounded), slot 55 is IsDead (units return
+0x274; unit_points' "IsServer" was wrong); CUnitServerVirtuals slots 21-50 (TakeUpWeapon 25, TameStage 39, GetAttackParams
50; 48 an empty base slot); `SpellRecord::unknown_10` (+0x10: 1 casts without noise / slowing the caster),
`ArmorRecord::layer` (+0xB8); the tick globals 007AFFC4 / 007B00D2 / 007B01F4 / 007B2798 are WorldServer()->tick_,
AIMap()->refreshUnits, Sectors() and timeWheel_; unit_skins' zone records are DressedUnitRecord 0x24 and FiguredUnitRecord
0x3C, its figure maker call CLinkFigureLoader::LoadFigure (morph floats passed as floats). Left: the __asm / float_control
blocks (combat_tick, combat, unit_motion_tick's 8), magic_trap's vtable constants, unit_skins' Text(0x...) strings. Next by
count: unit_create_stats.cpp.
