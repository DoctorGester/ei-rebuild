# Unit AI: brain, modes and actions (0x005D0000-0x005DC000)

Code: `src/game/unit_ai.h` (layouts, vtable slots, per-function comments: the structure reference), `unit_ai*.cpp` (brain
Think / lookups / actions in unit_ai.cpp; modes, brain, moves, looks, targets, memory, sight, attention, options, save,
fear, alarm, player, mode lifetime, mode slots). Verify: `tests/verify/verify_unit_ai*.cpp`. Scenarios: `unit_ai.txt`,
`unit_ai_fear.txt`, `script_exec` (UMClear, UM*, UMAg, Guard / Sentry), `combat_more` (a death), `world_load`, every spawn.
None of the rewrites is Rebirth-patched; the only patched callee is FatalError, called through `game::FatalError`.
Forwarders are in `unit_ai.h`, `unit_ai_modes.cpp`, `originals_ai.h`.

## Map

| Range | Contents |
|---|---|
| 005CA000-005CFFFF | the AI's eye (`AIViewer`), perception memory, heard calls, attention / attack options |
| 005D0030 | target choice among candidates (`AIAttention::Choose`, on brain + 0xA4), used by Aggression |
| 005D0A50-005D3760 | modes Guard, Patrol, Sentry, Follow, Suspection, Aggression, CorpseWatcher, Fear (Tamed, type 8, has its vtable elsewhere) |
| 005D37A0-005D4DB0 | Player mode (party units, type 6, vtable 0073F0F8): commands, tick, combat 005D4170, formation check 005D4860, load / save |
| 005D4DF0-005D5F20 | brain: UMClear, reset / init, SetOwner, IsNearPath, Think, OnObjectDeleted, alarm groups, CallAllies, PlanLooks, AddMode, mode lookups |
| 005D6140-005D6D20 | alarm / assembly records (0x4C bytes at brain + 0x94), the script's alarm reaction |
| 005D6F90-005D7F80 | actions: follow, move, attack, setters, FindStandPoint |
| 005D8310-005DBF70 | brain Load / Save, CList / CArray template instantiations and EH funclets |

- Entry: the unit controller (CUnit + 0x240) embeds the brain at +0x980; its tick 00552300 calls `AIBrain::Think`
  (005D53B0). Modes are 16-bit-refcounted (+4 INC word, released through 0040A3A0; a 32-bit increment would corrupt the
  padding). The script's UM* commands (SSCCmd::Execute cases 0x24-0x31, 0xC7-0xCA) build modes and add them with 005D5F60.
- The brain's perception memory is the sub-object at +0xD0 (005CB0F0 and 005CB700.. take it as `this`): lists of counted
  object references at +0xF4, +0x110 (visible), +0x12C, +0x148 (corpses), CArrays at +0x164 (reports) and +0x178 (heard
  calls); brain + 0xD8 `sightCells`, + 0xF0 `nearbyCountdown`; the alarm records (`AIAlarmRecordArray` of 0x4C bytes, the
  map spec's behaviour record) are at +0x90. +0x38 `callRadius` (10 after Init; equal to [Logic]
  CallForHelpRadiusDefault becomes CallForHelpRadius on load, NaN too); +0x68 FindStandPoint's cooldown (after a failure, a
  third of the distance in calls, at most 10, fail at once); +0x6C UpdateFollow's timer (acts from 8; an order sets 0 or 6);
  +0xA4 the attention (two CArrays of 0x28-byte AIAttackOption: `weapons` holds the weapon and the attack spells (kinds
  1..4), `spells` the helping ones (5..7); +0x28 the heal weight). UnitController has `stats` (+0x1E0), `protection` (+0x8E4)
  and `brain` (+0x980).
- Mode slot 9 (`Exclusive`): AddMode replaces a mode of another type when both answer 1. The mode list holds owning pointers
  (RefPtr::Share on add, RefPtr::Destroy on removal). Load creates modes by type (Player 6, Tamed 8 0073F0C4); an unknown
  type leaves Load at once (kept).
- Calls for help: `CallAllies` lists the units within +0x38 of the owner through the server's sector map; each allied one's
  memory hears kind 0x1A (005CBBA0 -> 005CCD40: earlier unflagged records of kind >= 0x1A are dropped, then {point, kind, 0,
  text} appended at +0x178).
- The AI's eye `AIViewer` (world server + 0x2838; map, eye, facing, cell, ready): 005CAA00 Update, 005CA1D0 Refresh (nearby
  list every 8..11 ticks from the sector cells of the memory's spiral cache), 005CA5A0 See (sight range x line of sight inside
  the cone `stats.unknown_044`, peripheral `stats.unknown_048`, sense life), 005CA410 Prepare (eye height, facing from the
  orientation). `CMapObjectVirtuals` slot 48 is `EyePoint`, slot 60 `Radius()`; LineOfSight is `LineOfSightOriginal`.
- 0x00480000-0x00490000 is not unit AI: FatalError (00480080, Rebirth-patched), exception objects, the HRESULT name table, a
  dead animation-state decoder (004803E0), configuration / diplomacy loading, containers, the particle system.

## Rewritten

All registered addresses are vtable or direct-call targets, so the game's own mode objects run them; callers inside the
area call rewritten neighbours through their addresses so the wrappers count. V: P pure, O output (original on a copy), D
differential (original first, state put back, ours, compared), C capture / recording streams, ours = consistency checks.

| Address | Name (file) | V |
|---|---|---|
| 005D53B0, 005D58D0 | AIBrain::Think, OnObjectDeleted | ours |
| 005D5DD0-005D5F20, 005D50E0 | FindMode, FindPatrolMode, FindPlayerMode, LeaderOrFollowed, PlayerModeLeader, AggressionEnemy, IsNearPath (AL) | P |
| 005D5080 | SetOwner (0x190 bytes and EAX) | O |
| 005D7520, 005D76A0, 005D7790, 005D7AE0-005D7E90, 005D6F90 | UpdateMove, UpdateAttack, StopAction, Walk, TurnTowards, AttackTarget, AttackWith, UseOn, CastAt, Follow, SetSeen, UpdateFollow | ours |
| 005D0A80, 005D0AF0, 005D0A50 | aimode::Priority50 (P), EnterIdle (random decay in [0.8, 1.2]), ClearAction | |
| 005D0B70, 005D1B50, 005D0BE0, 005D1700, 005D1B70, 005D1E10 | GuardMode::Set (O), SentryMode::Set (O), Guard / Patrol / Sentry / Follow Execute | ours |
| 005D1690, 005D16B0, 005D1E90, 005D1F80, 005D21A0, 005D2310 | PatrolMode::LastPoint, Evaluate, SuspectionMode::Evaluate, Execute, Enter, Leave | P with points / ours |
| 005D2390, 005D2580, 005D26D0, 005D2710, 005D27D0 | Aggression Evaluate (10000 with chosen = enemy, or -1000) / Execute / Enter / Leave, CorpseWatcherMode::Evaluate | ours |
| 005D3650, 005D3680, 005D2850 (O), 005D2960 (D), 005D30A0, 005CF170 | FearMode::Enter / Leave / Construct (script UMFear / UMAg, alarm assembly) / Evaluate / Execute, AIBrain::ApplyAttack (Fear's fight-back) (`unit_ai_fear.cpp`; fields `distance`, `spread`, `shelter`, `fleePoint` (z 1 while a checked flight point is pending)) | |
| 005D37A0, 005D1DE0, 005D4A90, 005D39E0-005D3D90, 005D37E0 (D), 005D3DE0 | PlayerMode / FollowMode::OnObjectDeleted, PlayerMode::Execute (every tick for party units), CommandWalk, GoTo, Attack, Use, Cast, CastAt, PathClear, SetLeader (a leader chain back to the unit stops the unit's own Player mode following first; CreateUnits) | ours |
| 005D4170, 005D3F50 (O), 005D4600 (O), 005D4860 (P) | UpdateCombat, AddPartyTargets, CollectParty, StaysWithParty (`unit_ai_player.cpp`) | |
| 005D5F60, 005D6140, 005D4DF0, 005D4E80, 005D4F70, 005CB0F0 | AddMode, RemoveModesOfType, ClearModes, Reset, Init, ResetMemory (`unit_ai_brain.cpp`) | ours |
| 005D6230, 005D64E0, 005D6300, 005D63B0, 005D63F0, 005D6440, 005D6480, 005D65B0 | StandardGuard, StandardSentry (script Guard / Sentry: exactly the five modes in order), AddGuardMode, AddSuspectionMode, AddAggressionMode, AddCorpseWatcherMode, AddFearMode, AddSentryMode (their only callers are the two above) | ours |
| 005D6630, 005D6D20, 005D9840 | ApplyBehaviour (a behaviour record as modes), SetAlarmRecords (the .mob loader), the records' SetSize on the template (`unit_ai_alarm.cpp`; every zone load, an alarm heard) | ours |
| 005D5990, 005CBBE0 (D), 005CC020, 005CC620, 005CC560 (P), 005CB700, 005CB7F0 (P) | CallAllies, Notice (Fear's Evaluate, spell effects), Forget, AIReportArray::Add / Contains (+0x164), UpdateMemory (each Think), IsEnemy | ours |
| 005CB260 (O), 005CB480 (O), 005CB670 (P), 005CBC80, 005CD500, 005CB850 (O, CheckNear), 005CBE90 | FindEnemies, FindEnemiesNear, NearestEnemy, ForceSee ("is forced to see unit"), AddCountedTail, MeasureExposure (each server tick: five exposure ratios into CUnit + 0x4C8), SeeCorpse (OnDamaged) (`unit_ai_memory.cpp`) | |
| 005CAA00, 005CA1D0, 005CA5A0, 005CA410 (O), 005CD210 (O) | AIViewer Update, Refresh, See, Prepare, MarkSightCells (the show-visibility effect) (`unit_ai_sight.cpp`) | ours |
| 005CC9D0, 00558D00, 00552CF0, 005CDD60, 005CD880 | AICallArray::Update (heard calls, each tick), AICallRecord::Destruct, UnitController::AddHostileMask, the heard calls' SetSize / Add on the template (`unit_ai_brain.cpp`) | ours against a model taken before |
| 005CF220 (O), 005CFE40, 005D0030 (D), 005CF9A0 (P), 005CF7F0 (P), 005CFE20 (P), 00545F40, 005CF720, 005CF780, 005CF3C0 (D, AL) | AIBrain::CurrentAttack, LookAround (`unit_ai_attention.cpp`; `this` the attention +0xA4, brain passed), AIAttention::Choose, Score, IsUsable, AIDistance2, AIOrder Clear / MakeAttack / MakeCast, CanReach (spell aimed at no object vs a large size-class-0 unit: no; else the order check from the unit, or from the end of a path search with the target lifted) (`unit_ai_targets.cpp`) | |
| 005CEED0 (D), 005CEA30 (O), 005CEA60, 005CEBD0, 005CEDC0 (P), 005CE9B0 (P), 005D9250, 005CE860, 005CE920 (C), 005D9B80 | AIAttention::Build (the option lists, when a unit's controller is set up (005535F0 / 00553610) and by 005D6630), InitOption, AddWeaponOption / AddSpellOption, SpellKind (spell type -> kind table), AIAttackOption::Weight, CArray<AIAttackOption>::SetSize, AIAttackOption Load / Save, LoadAttackOptions (`unit_ai_options.cpp`; unit_ai, units, spells_cast, ui_saveload) | |
| 005D5B40 (D), 005D7F80 (D) | PlanLooks, FindStandPoint (`unit_ai_moves.cpp`; random state put back; 4 ULPs) | |
| 005573F0 (O), 005D8EE0, 005D8B60, 005D9DE0 (O), 00545B20, 005D12B0, 005D93B0, 005D9F30 | CArray<AILook> Init, Resize, CopyFrom, ElementAt, AIMoveOrder::Clear, PatrolMode::AppendPoint, the patrol point array's Add / SetAtGrow (`unit_ai_looks.cpp`; PatrolPoint::operator=, PatrolPointElements) | ours |
| 005D3760, 005D0F60, 005D1240, 005D1220, 005D2950, 005D2930, 005D1210 (P), 005D2920 (P), 005D0A90, 005D8850, 005D6D10, 005D8CF0, 005D8CD0, 005D9E10, 005D9830 | PlayerMode::Construct, PatrolMode::Construct, Patrol / Fear destructor and deleting destructor, GetType (1, 7), the abstract base's Execute (remark, LookAround), the abstract bases' constructors (0073D15C / 0073D22C), `PatrolPointArray` destructors, ZeroAttackOptions, CList::GetNext (`unit_ai_mode_lifetime.cpp`; `verify_unit_ai_templates.cpp`) | O / ours |
| 005D8310, 005D8860, 005CC1E0, 005CC350, 005D9010, 005D91B0, 005D9D40 | brain Load / Save (`unit_ai.h` has the order), memory load / save, looks load / save (`unit_ai_save.cpp`), `RefPtr::ShareAssign` (`core_refcount.cpp`) | saves C, loads ours; world_load |
| slots 11 / 12 | Load / Save of Guard, Sentry, Suspection, Fear (Save only: Follow, Aggression, Player) | C (load: the original reads into a copy, ours from the recording) |

Verify lessons (X12): FearMode::Construct once wrote -1312.0f as the unset x; the original stores 0xC4A42000, which is
-1313.0f. UpdateMemory's callees take over a noticed point inline (005CC9D0: a delayed sound, level 900, kind -3; 005CAA00:
a corpse seen, 800 / -3, an enemy lost from sight, 1000 / -2), so the final add uses the new level and kind; the check counts
CopyPoint2 (005CA050) calls (each inline notice calls it first, through its address) to tell the cases apart. Comparing
against the original on a copy is not possible: the callees use rand(), release objects and change lists and arrays whose
buffers a copy would share.

## Original bugs and oddities kept on purpose

- Moves: PlanLooks' wait of the seventh (untested) draw is kept when all six line-of-sight tests fail; UpdateFollow ignores
  the result of its side-step path search and only reads the path's target; PathClear ignores its `this`. Test paths on the
  stack (`ScopedPath`, `ai_scoped_path.h`) mirror the original's constructors (two cell lists of 0x80, the point array,
  Clear).
- The mode adders (005D6300-005D65B0) write the new mode's +4 before testing the allocation and pass a failed one to AddMode,
  which calls its GetType (ours tests first, then also passes null). A new Suspection mode's point and level and a new Fear
  mode's +0x14..+0x20 stay uninitialised. CallAllies formats its message only for the console filter but always builds it.
- `IsEnemy` (the name is the old guess) answers 1 for the same side, the same player, or an object in the +0x110 list, so
  UpdateAttack gives up only on a unit of another side and player that is not listed. UpdateMemory adds the noticed kind
  (negative: decay) to the noticed level every tick. LookAround's order for the player check leaves the point uninitialised
  (ours zero), like AttackTarget's order for 0054C8B0 (+0x0C..+0x17).
- Notice measures the distance with the new point's own z (the memory keeps no z); Forget compares the report entries with
  its argument and the list elements with its own counted copy (the same pointer).
- Think's "new motivation" message (console ostream 007C54E8; not `expect`ed) prints an empty name for mode type 9 or above
  10. `IsNearPath` sets only AL on its first "near" return and keeps NaN in the clamp of the segment parameter. Player's
  CommandCast passes Cast a third argument whose upper bytes are the mode pointer's (ours the zero-extended run flag). The
  Save functions write scalars from stack copies (`WriteCopy`), which only matters to a capturing stream.
- Guard's Execute counts failed searches near its point; after 100 it also searches around the unit, on every tick until
  one succeeds (only success resets the count). Patrol with no points calls FatalError "Unit %i has no patrol points,
  switching to guard" and adds a point at the unit (it does not switch). Suspection's Execute computes the "near the point"
  distance only while a move is under way; its level drops by 5 per tick near the point, by 50 in combat. Aggression's
  Execute asks for the current attack twice in a row (two 005CF220 calls).
- AppendPoint rounds 2x to nearest, so its point is (n + 0.5) / 2, 0..0.5 above the asked one, not its half-cell's middle.
  Score's group bonus counts the group minus one for kinds 1 and 4..7, the whole group for 2 and 3; kind 3 multiplies by
  (effect / 4) / 2; the weapons are only scored when `alliesOnly` is off; the order checked for a target off the path has an
  uninitialised point (ours zero). Choose keeps its scored candidates in a local vector instead of the original's CList (heap
  traffic differs, not the result).
- FindEnemiesNear asks the visible unit's IsHostileTo(self) where the others ask the unit's; ForceSee's "sight" test compares
  the observer's ranges as the original does (forced when at or beyond either of them, or when the object attacks the unit).
  AIViewer::Update lists an allied corpse in +0x12C twice (the inlined AddTail, then 005CD500); See skips a dead hostile unit
  unless the unit sees all; "using sense life " is printed without a line end before the "sees unit" line.
- Patrol's destructor leaves the point array's pointer and count behind (freed). Fear with `flag` and no player unit in
  sight notices an unset local (ours zero; only matters for a distance above 1e15). StaysWithParty reads a party unit's
  missing Player mode at address 0x0C in the original (a crash; ours skips it); UpdateCombat attacks the chosen target with
  the weapon even when the choice was a spell.

## Skipped

- Allocation-heavy bookkeeping, rare: 005D1930 / 005D1A40 Patrol Load / Save (Ghidra garbles the frames), 005D0F90 / 005D2880
  clones, destructors; 005D1E60 Follow Load, 005D2770 Aggression Load, 005D4CD0 Player Load (a ScriptGroup for versions
  0x46..0x5D) run only on game load; 005D7F40 (seen object is an ally) leaves the caller's EAX upper bytes in the result;
  constant slots are trivial. 00522B20 (largest damage) stays a forwarder (units area); also 00522AE0 ExcessDamage,
  0054C960 CheckOrderAt.
- Library: 005CDA40 (CArray<AICallRecord>::RemoveAt), 005CDB00 (CList AddHead), 00582140 / 00582730 / 00583330 (vector
  destructors), shape cache helpers 005BCCB0 / 005BE320 / 005BE450 / 005BE730.

## Risks

Most functions are verified "ours only" with consistency checks: a wrong branch that leaves a consistent state would pass;
the scenarios (patrol, guard, sentry, follow, aggression, fear) are the behavioural check. Floats: `double` locals for values
the original keeps on the FPU stack, `volatile float` where it stores to a float.

## A-W8: next candidates

The area 0x005D0000-0x005DC000 is done. Next (world client, world.md "Open items"): 00566360 (day-time lights: the
LoadLights tables interpolated by hour, sun direction, fog colour), 0056C750 (shadow / decal pass over the object list;
Ghidra garbles its frame), 0056D150 (a ground decal near a light, STLport temporaries), then the largest unported functions
of 0x540000-0x5E0000 (config/functions.csv).
