# Unit AI: brain, modes and actions (0x005D0000-0x005DC000)

Code: `src/game/unit_ai.h` (layouts, vtable slots, per-function comments: the structure reference),
`unit_ai.cpp` (brain: Think, lookups, actions), `unit_ai_modes.cpp` (mode virtual slots, player
commands). Verify: `tests/verify/verify_unit_ai.cpp`. Scenario: `tests/scenarios/unit_ai.txt`.

## Map

| Range | Contents |
|---|---|
| 005D0030 | target choice among candidates (on brain + 0xA4), used by Aggression |
| 005D0A50-005D3760 | modes Guard, Patrol, Sentry, Follow, Suspection, Aggression, CorpseWatcher, Fear (Tamed, type 8, has its vtable elsewhere) |
| 005D37A0-005D4DB0 | Player mode (party units): commands, tick, combat 005D4170, formation check 005D4860, load/save |
| 005D4DF0-005D5F20 | brain: UMClear, reset/init, SetOwner, IsNearPath, Think, OnObjectDeleted, alarm groups, CallAllies, PlanLooks, AddMode, mode lookups |
| 005D6140-005D6D20 | alarm/assembly records (0x4C bytes at brain + 0x94), the script's alarm reaction |
| 005D6F90-005D7F80 | actions: follow, move, attack, setters, FindStandPoint |
| 005D8310-005DBF70 | CList/CArray template instantiations and EH funclets |

- The brain's perception memory is the sub-object at +0xD0 (005CB0F0 and 005CB700.. take it as `this`): lists of counted
  object references at +0xF4, +0x110 (visible), +0x12C, +0x148 (corpses), CArrays at +0x164 (reports) and +0x178; the alarm
  records (CArray of 0x4C bytes) are at +0x90. Mode slot 9 (`Exclusive`): AddMode replaces a mode of another type when both
  answer 1. The mode list holds owning pointers (RefPtr::Share on add, RefPtr::Destroy on removal).
- Calls for help: `CallAllies` lists the units within +0x38 (10 after Init) of the owner through the server's sector map;
  each allied one's memory hears kind 0x1A (005CBBA0 -> 005CCD40: earlier unflagged records of kind >= 0x1A are dropped,
  then {point, kind, 0, text} appended at +0x178).
- Entry: the unit controller (CUnit + 0x240) embeds the brain at +0x980; its tick 00552300 calls
  `AIBrain::Think` (005D53B0). Modes are 16-bit-refcounted (+4, released through 0040A3A0).
- The script's UM* commands (SSCCmd::Execute cases 0x24-0x31, 0xC7-0xCA) build modes and add them
  with 005D5F60.
- 0x00480000-0x00490000 is not unit AI: FatalError (00480080, Rebirth-patched), exception objects,
  the HRESULT name table, a dead animation-state decoder (004803E0), configuration/diplomacy
  loading, containers, the particle system. Nothing rewritten for AI there.

## Rewritten

All registered addresses are vtable or direct-call targets, so the game's own mode objects run
them. "Ours only" means a consistency check on the state afterwards.

| Address | Name | Verify |
|---|---|---|
| 005D53B0 | AIBrain::Think | ours only: corpse list empty, nothing seen, mode in charge is listed and referenced |
| 005D5DD0-005D5F20 | AIBrain::FindMode, FindPatrolMode, FindPlayerMode, LeaderOrFollowed, PlayerModeLeader, AggressionEnemy | pure |
| 005D50E0 | AIBrain::IsNearPath | pure (AL compared) |
| 005D5080 | AIBrain::SetOwner | original on a copy, all 0x190 bytes and EAX compared |
| 005D58D0 | AIBrain::OnObjectDeleted | ours only: neither target nor seen is the object |
| 005D7520, 005D76A0, 005D7790 | UpdateMove, UpdateAttack, StopAction | ours only: action / target / looks invariants |
| 005D7AE0-005D7E90 | Walk, TurnTowards, AttackTarget, AttackWith, UseOn, CastAt, Follow, SetSeen | ours only: action and target as set |
| 005D0A80, 005D0AF0, 005D0A50 | aimode::Priority50, EnterIdle (random decay in [0.8, 1.2]), ClearAction | pure / ours only |
| 005D0B70, 005D1B50 | GuardMode::Set, SentryMode::Set | original on a copy, object compared |
| 005D0BE0, 005D1700, 005D1B70, 005D1E10 | Guard / Patrol / Sentry / Follow Execute | ours only: a busy brain untouched, an idle one starts a move / follows |
| 005D1690, 005D16B0 | PatrolMode::LastPoint, Evaluate | pure with points; without: ours only, -1000 and one point added |
| 005D1E90 | SuspectionMode::Evaluate | no seen object: original on a copy; with one (may add to the memory): ours only |
| 005D1F80, 005D21A0, 005D2310 | Suspection Execute / Enter / Leave | ours only |
| 005D2390, 005D2580, 005D26D0, 005D2710 | Aggression Evaluate / Execute / Enter / Leave | ours only (10000 with chosen = enemy, or -1000) |
| 005D27D0 | CorpseWatcherMode::Evaluate | ours only: -1000, one corpse fewer |
| 005D3650, 005D3680 | FearMode::Enter / Leave | ours only |
| 005D37A0, 005D1DE0 | PlayerMode / FollowMode::OnObjectDeleted | ours only |
| 005D4A90 | PlayerMode::Execute (every tick for party units) | ours only: state in 0..2, a following brain has a target |
| 005D39E0-005D3D90 | PlayerMode::CommandWalk, GoTo, Attack, Use, Cast, CastAt | ours only: state / action / target / run flag |
| 005D5F60, 005D6140, 005D4DF0, 005D4E80, 005D4F70, 005CB0F0 | AIBrain::AddMode, RemoveModesOfType, ClearModes, Reset, Init, ResetMemory (`unit_ai_brain.cpp`) | ours only: list walk agrees with count and tail, one mode of the added type, the removed type gone, cleared fields; scenarios `unit_ai` / `script_exec` (UMClear, UM*, UMAg), every spawn (Init), a death (Reset, `combat_more`) |
| 005D6230, 005D64E0 | AIBrain::StandardGuard, StandardSentry (script Guard / Sentry) | ours only: exactly the five modes in order, point, fear / aggression fields; scenario `script_exec` |
| 005D6300, 005D63B0, 005D63F0, 005D6440, 005D6480, 005D65B0 | AddGuardMode, AddSuspectionMode, AddAggressionMode, AddCorpseWatcherMode, AddFearMode, AddSentryMode | ours only: last mode's type and fields (their only callers are the two above, so the wrappers do not run in practice) |
| 005D5990 | AIBrain::CallAllies | ours only: the same sector-map query first; every ally in range has the call (point, kind 0x1A) as its last heard record |
| 005CBBE0 | AIBrain::Notice (memory) | differential: original first, noticed fields put back, ours; result and fields compared (reached through Fear's Evaluate and spell effects; our modes call it directly) |
| 005CC020 | AIBrain::Forget (memory) | ours only: the object is in no memory list nor the reports (OnObjectDeleted calls it through its address) |
| 005CC620, 005CC560 | AIReportArray::Add, Contains (+0x164) | Add ours only (listed once, what kept at the maximum); Contains pure (AL); reached through 005D0030 |
| 005CB700 | AIBrain::UpdateMemory (memory, each Think; Think calls it through its address) | ours only: no report below 0; the level before the final add is the old one (kind unchanged), or after a notice inside (CopyPoint2 called) 900 / 800 with kind -3 or 1000 with kind -2, above the old level |
| 005CB7F0 | AIBrain::IsEnemy (memory) | pure |
| 005CF220, 005CFE40 | AIBrain::CurrentAttack, LookAround (`unit_ai_attention.cpp`; `this` the attention +0xA4, brain passed) | CurrentAttack output (original into a seeded scratch pair); LookAround ours only (attack action has a target); reached through 005D0030, 005D4170 and the Guard / Patrol / Sentry Execute |
| 005D2850, 005CA050 | FearMode::Construct (script UMFear / UMAg, alarm assembly), CopyPoint2 (events, heard calls) | output: the original on a copy, all bytes and the returned pointer compared |
| 005D5B40, 005D7F80 | AIBrain::PlanLooks, FindStandPoint (`unit_ai_moves.cpp`) | differential: the original on a copy (PlanLooks: an empty look array; FindStandPoint: a scratch point) from the same random state, ours after the state is put back; looks / point within 4 ULPs, cooldown, result and random state compared |
| 005D6F90 | AIBrain::UpdateFollow | ours only: a dead or missing leader ends the action, else the timer counts on or restarts at 0 / 6 |
| 005D37E0 | PlayerMode::PathClear | original, then ours: results compared, the unit back at its position (ui_order_move, movement) |
| 005CC9D0, 00558D00, 00552CF0 | AICallArray::Update (heard calls, each tick), AICallRecord::Destruct, UnitController::AddHostileMask (`unit_ai_brain.cpp`) | ours only against a model taken before (calls left in order one count lower; the first run-out call without a mask becomes the noticed point at 900 / -3 when the level was lower); UpdateMemory calls Update through its address; the inline notice calls CopyPoint2 through its address so the UpdateMemory check still counts it |
| Load / Save of Guard, Sentry, Suspection, Fear (Save only: Follow, Aggression, Player) | slots 11 / 12 | load: original reads the real stream into a copy (bytes recorded), ours from the recording, all bytes consumed; save: both into capture streams, bytes and write sizes compared |

X12 (2026-10-05), the two A-W failures, back on (out of `tests/verify/disabled.txt`):
- 005D2850 FearMode::Construct wrote -1312.0f as the unset x; the original stores 0xC4A42000, which is -1313.0f
  (the AddFearMode / StandardGuard check had the same wrong value, and several comments said -1312 for that bit pattern).
- 005CB700 UpdateMemory was right; its check was not. Both callees take over a noticed point inline (005CC9D0: a delayed
  sound, level 900, kind -3; 005CAA00: a corpse seen, 800 / -3, an enemy lost from sight, 1000 / -2), so the final add uses
  the new level and kind. The check now counts CopyPoint2 calls (each inline notice calls it first) to tell the two cases
  apart; it depends on the 005CA050 wrapper being hooked. Comparing against the original on a copy is not possible: the
  callees use rand(), release objects and change lists and arrays whose buffers a copy would share.

None is in `docs/rebirth-patched-functions.txt`; the only patched callee is FatalError, called
through `game::FatalError` (EI_ORIGINAL). Forwarders are in `unit_ai.h`, `unit_ai_modes.cpp`,
`originals_ai.h`; the standalone build must replace them.

## Original bugs and oddities kept on purpose

- A-W2 (`unit_ai_moves.cpp`): the brain's +0x68 is FindStandPoint's cooldown (after a failure, a third of the distance in calls, at most
  10, fail at once) and +0x6C UpdateFollow's timer (acts from 8; an order sets 0 or 6). PlanLooks' wait of the seventh (untested) draw is
  kept when all six line-of-sight tests fail; UpdateFollow ignores the result of its side-step path search and only reads the path's target;
  PathClear ignores its `this`. Test paths on the stack (`ScopedPath`) mirror the original's constructors (two cell lists of 0x80, the
  point array, Clear). Map objects' slots 52 / 55 are called through `CMapObjectVirtuals` (map_object.h).

- The mode adders (005D6300-005D65B0) write the new mode's +4 before testing the allocation and pass a failed one to AddMode,
  which calls its GetType (ours tests first, then also passes null). A new Suspection mode's point and level and a new Fear
  mode's +0x14..+0x20 stay uninitialised. CallAllies formats its message only for the console filter but always builds it.
- `IsEnemy` (the name is the old guess) answers 1 for the same side, the same player, or an object in the +0x110 list, so
  UpdateAttack gives up only on a unit of another side and player that is not listed. UpdateMemory adds the noticed kind
  (negative: decay) to the noticed level every tick. LookAround's order for the player check leaves the point uninitialised
  (ours zero), like AttackTarget.
- Notice measures the distance with the new point's own z (the memory keeps no z); Forget compares the report entries with
  its argument and the list elements with its own counted copy (the same pointer).

- Think's "new motivation" message prints an empty name for mode type 9 or above 10. `IsNearPath` sets only AL on its first "near" return (callers test AL) and keeps NaN in the clamp of the segment parameter.
  `AttackTarget` passes the player's order check (0054C8B0) an order whose point (+0x0C..+0x17) the original leaves uninitialised (ours zeroes it). Player's CommandCast passes Cast a third argument whose upper bytes
  are the mode pointer's (ours passes the zero-extended run flag). The Save functions write scalars from stack copies (`WriteCopy`), which only matters to a capturing stream.
- Guard's Execute counts failed searches near its point; after 100 it also searches around the unit, on every tick until one succeeds (only success resets the count). Patrol with no points calls FatalError "Unit %i has
  no patrol points, switching to guard" and adds a point at the unit (it does not switch). Suspection's Execute computes the "near the point" distance only while a move is under way; its level drops by 5 per tick near
  the point, by 50 in combat. Aggression's Execute asks for the current attack twice in a row (two 005CF220 calls).

## Skipped and why

- Large float / path code: 005D2960 / 005D30A0 Fear Evaluate / Execute, 005D4170 Player combat, 005D6630 /
  005D6D20 alarm assembly. 005D4860 StaysWithParty walks the player's unit hash map through iterator helpers (00455830 / 00455890 / 004558B0) with an
  unclear frame.
- Allocation-heavy bookkeeping, rare: 005D1930 / 005D1A40 Patrol Load / Save (Ghidra garbles the frames), 005D0F90 / 005D2880 clones,
  destructors; 005D1E60 Follow Load, 005D2770 Aggression Load, 005D4CD0 Player Load (a ScriptGroup for versions 0x46..0x5D) run only on game load; 005D7F40 (seen object is an ally) leaves the caller's EAX upper bytes in the
  result; 005D1210 / 005D2920 (GetType of Patrol / Fear) and other constant slots are trivial.

## Risks

- Most are verified "ours only" with consistency checks: a wrong branch that leaves a consistent state would pass; the scenario (patrol, guard, sentry, follow, aggression) is the behavioural check. Think writes the "new
  motivation" line through the console ostream 007C54E8 like the original; the scenario does not `expect` it (the stream may not be the in-game console). Floats: `double` locals for values the original keeps on the FPU
  stack, `volatile float` where it stores to a float. `AIMode::refs` is a 16-bit count (INC word); a 32-bit increment would corrupt the padding.

## A-W5 (2026-10-06)

- `unit_ai_looks.cpp`: 005573F0 / 005D8EE0 / 005D8B60 / 005D9DE0 (CArray<AILook> Init, Resize, CopyFrom, ElementAt), 00545B20
  (AIMoveOrder::Clear), 005D12B0 (PatrolMode::AppendPoint). The modes keep calling them through their addresses (the old
  forwarder names) so the wrappers count them. Verify: Init / ElementAt output against the original; the rest ours only against
  a model taken before. Scenario `unit_ai` (UMPatrolAddPoint; every Guard / Patrol / Sentry move). Kept: AppendPoint rounds
  2x to nearest, so its point is (n + 0.5) / 2, 0..0.5 above the asked one, not its half-cell's middle. The patrol point
  array's SetSize 004C9970 moved to `originals_ai.h` (also used by script_exec.cpp).
- `unit_ai_targets.cpp`: 005D0030 AIAttention::Choose (the target choice), 005CF9A0 Score, 005CF7F0 IsUsable, 005CFE20
  AIDistance2, AIOrder 00545F40 Clear / 005CF720 MakeAttack / 005CF780 MakeCast. The attention at brain + 0xA4 is now a struct
  (two CArrays of 0x28-byte AIAttackOption: weapons, spells; +0x28 the heal weight); UnitController gained `stats` (+0x1E0),
  `protection` (+0x8E4) and `brain` (+0x980). Verify: Score / IsUsable / AIDistance2 pure against the original; Choose
  differential (original first, the reports it added or raised put back, ours; pair and reports compared); the orders ours
  only. Forwarders: 00522AE0 ExcessDamage (originals_ai.h), 0054C960 CheckOrderAt, 005CF3C0 CanReach (unit_ai.cpp).
  Choose keeps its scored candidates in a local vector instead of the original's CList (heap traffic differs, not the result).
  Kept: Score's group bonus counts the group minus one for kinds 1 and 4..7, the whole group for 2 and 3; kind 3 multiplies
  by (effect / 4) / 2; the weapons are only scored when `alliesOnly` is off; the order checked for a target off the path
  has an uninitialised point (ours zero).
- `unit_ai_memory.cpp`: 005CB260 FindEnemies, 005CB480 FindEnemiesNear, 005CB670 NearestEnemy, 005CBC80 ForceSee ("is forced to
  see unit"), 005CD500 AddCountedTail. Verify: the two list queries output (the original into an empty scratch list, element
  sequences compared), NearestEnemy pure, ForceSee and AddCountedTail ours only (list deltas). Kept: FindEnemiesNear asks the
  visible unit's IsHostileTo(self) where the others ask the unit's; ForceSee's "sight" test compares the observer's ranges as
  the original does (forced when at or beyond either of them, or when the object attacks the unit).
- Also in `unit_ai_memory.cpp`: 005CB850 MeasureExposure (each server tick: five exposure ratios into CUnit + 0x4C8; output
  verify against the original, CheckNear) and 005CBE90 SeeCorpse (OnDamaged; ours only). `CMapObjectVirtuals` slot 48 is now
  `EyePoint` (map_object.h); LineOfSight is called through its address (`LineOfSightOriginal`, originals_ai.h).
- `unit_ai_sight.cpp` (the leftover 005CAA00): the AI's eye `AIViewer` (world server + 0x2838; map, eye, facing, cell, ready):
  005CAA00 Update, 005CA1D0 Refresh (nearby list every 8..11 ticks from the sector cells of the memory's spiral cache, brain + 0xD8
  `sightCells`; brain + 0xF0 is `nearbyCountdown`), 005CA5A0 See (sight range x line of sight inside the cone `stats.unknown_044`,
  peripheral `stats.unknown_048`, sense life), 005CA410 Prepare (eye height, facing from the orientation). Each calls the next
  through its address. Verify: Prepare output on a copy; the others ours only (countdown / list consistency, no repeats, no dead
  unit left visible). Kept: Update lists an allied corpse in +0x12C twice (the inlined AddTail, then 005CD500); See skips a dead
  hostile unit unless the unit sees all; "using sense life " is printed without a line end before the "sees unit" line.
  `CAIMap::SectorMap()` (ai_map.h) replaces ai_path.cpp's offset helper.
- `unit_ai_mode_lifetime.cpp`: 005D3760 PlayerMode::Construct, 005D0F60 PatrolMode::Construct, 005D1240 / 005D1220 Patrol
  destructor / deleting destructor, 005D2950 / 005D2930 Fear's, 005D1210 / 005D2920 GetType (1, 7), 005D0A90 the abstract base's
  Execute (remark, LookAround). Verify: constructors output on a copy (the fields they write), GetType pure, the rest ours only.
  Kept: Patrol's destructor leaves the point array's pointer and count behind (freed).
- Also there: 005D8850 / 005D6D10 the abstract bases' constructors (vtables 0073D15C / 0073D22C), 005D8CF0 / 005D8CD0 the
  patrol point array's destructors (`PatrolPointArray`), 005D9E10 ZeroAttackOptions (ConstructElements), 005D9830 ListGetNext
  (CList::GetNext). Verify (`verify_unit_ai_templates.cpp`): constructors and GetNext against the original on copies, the rest
  ours only.

## A-W6 (2026-10-07)

- `unit_ai_options.cpp`: 005CEED0 AIAttention::Build (the option lists, run when a unit's controller is set up, 005535F0 /
  00553610, and by 005D6630), 005CEA30 InitOption, 005CEA60 / 005CEBD0 AddWeaponOption / AddSpellOption, 005CEDC0 SpellKind
  (spell type -> kind table), 005CE9B0 AIAttackOption::Weight, 005D9250 (CArray<AIAttackOption>::SetSize, registered on the
  template; `AIAttackOptions` is now that alias). Build calls the helpers through their addresses. `weapons` holds the weapon
  and the attack spells (kinds 1..4), `spells` the helping ones (5..7). Verify: Build differential (original on a copy with
  copied arrays; lists and healWeight compared), InitOption output, SpellKind / Weight pure, the rest ours only. Scenarios:
  `unit_ai`, `units`, `spells_cast` (any AI unit created). 00522B20 (largest damage) stays a forwarder (units area).
- Also there: 005CE860 / 005CE920 AIAttackOption Load / Save and 005D9B80 LoadAttackOptions (the array: count, SetSize, each
  option through its address). Verify: Save differential through a recording AIStream; the loads ours only. Scenarios:
  `ui_saveload` (a save and a load with AI units). Next candidates: 005D9D40 (a RefPtr assign), the brain's Load / Save
  005D8310 / 005D8860, 005CF3C0 CanReach (an AIPath local), 005D3DE0 (follow leader; its mode type 6 layout unknown).

## A-W7 (2026-10-07)

- `unit_ai_targets.cpp`: 005CF3C0 CanReach (spell aimed at no object vs a large size-class-0 unit: no; else the order check
  from the unit, or from the end of a path search to the target with the target lifted off the sector map). The stack path is
  `ScopedPath` (`ai_scoped_path.h`, shared with unit_ai_moves.cpp). Verify: differential (AL). Scenarios: `unit_ai`, `combat`.
  Map object slot 60 is now `Radius()` (map_object.h).
- `unit_ai_brain.cpp`: 005D8310 / 005D8860 brain Load / Save (`unit_ai.h` has the order). Load creates modes by type
  (Player is type 6, vtable 0073F0F8; Tamed type 8, 0073F0C4); an unknown type leaves Load at once (kept). A call radius
  (+0x38, now `callRadius`) equal to [Logic] CallForHelpRadiusDefault becomes CallForHelpRadius (NaN too). `alarmRecords` is
  now `AIAlarmRecordArray`. `unit_ai_save.cpp`: 005CC1E0 / 005CC350 memory load / save, 005D9010 / 005D91B0 looks load / save.
  `core_refcount.cpp`: 005D9D40 `RefPtr::ShareAssign`. Helpers go through their addresses (`originals_ai.h`; 0052AF20 moved to
  `originals_units.h`). Verify (`verify_unit_ai_save.cpp`): saves differential through recording streams; loads and
  ShareAssign ours only (list / count consistency). Scenario: `world_load` (autosave, then load savea).
- `unit_ai_modes.cpp`: 005D3DE0 PlayerMode::SetLeader (a leader chain back to the unit makes the unit's own Player mode stop
  following first). Verify ours only. Reached by CreateUnits (every new game).
- Library: 005CDA40 (CArray<AICallRecord>::RemoveAt), 005CDB00 (CList AddHead), 00582140 / 00582730 / 00583330 (vector
  destructors), shape cache helpers 005BCCB0 / 005BE320 / 005BE450 / 005BE730.
- Next: 005D2960 / 005D30A0 Fear Evaluate / Execute, 005D6630 / 005D6D20 alarm assembly, 005D4170 Player combat,
  005D4600, 005D4860, 005D9F30, 005D93B0, 005CD880, 005CD210; client 00566360 / 0056C750 / 0056D150.
