# Unit AI: brain, modes and actions (0x005D0000-0x005DC000; 0x00480000-0x00490000 mapped)

Code: `src/game/unit_ai.h` (layouts), `src/game/unit_ai.cpp` (the brain: Think, lookups, actions),
`src/game/unit_ai_modes.cpp` (the mode virtual slots and the player's commands).
Verify wrappers: `tests/verify/verify_unit_ai.cpp`. Scenario: `tests/scenarios/unit_ai.txt`.

Shared files touched: none (no shared header was changed; everything is in the new files).

## What lives in these ranges

| Range | Contents |
|---|---|
| 005D0030 | target choice among candidates (005D0030, on brain + 0xA4), used by Aggression |
| 005D0A50-005D3760 | mode classes: Guard, Patrol, Sentry, Follow, Suspection, Aggression, CorpseWatcher, Fear (vtables in the table below) |
| 005D37A0-005D4DB0 | Player mode (party units): commands, tick, combat (005D4170), formation check (005D4860), load/save |
| 005D4DF0-005D5F20 | brain: clear modes (UMClear), reset/init, SetOwner, IsNearPath, Think, OnObjectDeleted, alarm groups, CallAllies, PlanLooks, AddMode, mode lookups |
| 005D6140-005D6D20 | alarm/assembly records (0x4C bytes at brain + 0x94), the script's alarm reaction |
| 005D6F90-005D7F80 | actions: follow (005D6F90), move (005D7520), attack (005D76A0), walk/turn/attack/use/cast/follow/seen setters, FindStandPoint (005D7F80) |
| 005D8310-005DBF70 | CList/CArray template instantiations for the above (look arrays, corpse lists, mode lists) and their EH funclets |
| 005DC000- | the console (not this area) |

The 0x00480000-0x00490000 range is not unit AI: it holds `FatalError` / `FatalErrorCode`
(00480080, 004800E0; Rebirth patches 00480080), exception objects with formatted messages
(00480250, 00480300), the DirectX HRESULT -> name table (00480B80), a debug decoder of an
animation state word into "State Attack." / "Action Walk." text (004803E0, no callers: dead
code), configuration/diplomacy loading (00481660, 00481F00), container templates (00482000-
00484700), body-part tables (00484C00) and the particle system (00485540 CParticle::Create and
the effect classes up to 0048FDB0). Nothing there runs per AI unit, so nothing was rewritten.

## Structures recovered

Names provisional; offsets are `static_assert`ed in `unit_ai.h`.

- **UnitController** (CUnit + 0x240 for AI units; its tick 00552300 calls AIBrain::Think): +0x1C
  unit, +0x164 state (5: the brain does nothing), +0x174 busy, +0x18C in combat, +0x190 / +0x198
  current command and its target, +0x1B4 / +0x1BC queued command and target, +0x980 the brain,
  +0xB10 the voice (005AEA60 says a remark).
- **AIBrain** (controller + 0x980, 0x190 bytes): +0x00 owner, +0x04 action (0 none, 1 walk,
  2 move to `actionPoint` then the queued looks, 3 attack, 4 cast, 5 use, 6 follow), +0x08 action
  point, +0x14 `CArray<AILook>` of looks queued for after the move, +0x28 action time (-10000 no
  look, -9999 turning, else the game time the look ends; also the spell being cast), +0x2C target
  (counted), +0x30 seen object (counted, dropped every tick), +0x34 alerted, +0x35 alarm kind,
  +0x3C alert mode, +0x40 alarm countdown, +0x44/+0x48 two radii, +0x4C alertness decay,
  +0x50/+0x5C previous/current chosen position (x = -1312: unset), +0x70 `CTypedPtrList` of modes,
  +0x8C mode in charge (counted, 16-bit count), +0x94/+0x98 alarm records, +0xA4 attention
  (005CFE40 look around, 005CF220 current attack, 005D0030 target choice), +0xD0 perception memory
  (005CB700 update, 005CBBE0 notice, 005CB7F0 is-enemy, 005CBC80 remember, 005CC020 forget, with
  the noticed point/level/kind at +0xE0..+0xEC), +0x110 list of visible units, +0x148 list of seen
  corpses (counted), +0x164 report list (005CC620 add, 005CC560 contains), +0x18C sees an enemy.
- **Modes** (16-bit reference count at +4, released through 0040A3A0). Virtual slots: 0 deleting
  destructor, 1 (0x412580), 2 destructor body, 3 clone, 4 Evaluate(brain) -> priority, 5 Execute,
  6 Enter, 7 Leave, 8 GetType, 9 (bool), 10 OnObjectDeleted, 11 Load, 12 Save.

| Type | Mode | vtable | size | Evaluate | Execute | Enter / Leave |
|---|---|---|---|---|---|---|
| 0 | Guard | 0073D128 | 0x20 | 005D0A80 (50) | 005D0BE0 | 005D0AF0 / no-op |
| 1 | Patrol | 0073FF40 | 0x20 | 005D16B0 | 005D1700 | 005D0AF0 / no-op |
| 2 | Sentry | 0073D190 | 0x14 | 005D0A80 | 005D1B70 | 005D0AF0 / no-op |
| 3 | Suspection | 0073D1F8 | 0x18 | 005D1E90 | 005D1F80 | 005D21A0 / 005D2310 |
| 4 | Aggression | 0073D260 | 0x18 | 005D2390 | 005D2580 | 005D26D0 / 005D2710 |
| 5 | CorpseWatcher | 0073D294 | 8 | 005D27D0 (-1000) | no-op | 005D0A50 / no-op |
| 6 | Player | 0073F0C4/0073F0F8 | 0x24 | 00552140 | 005D4A90 | 005D0A50 / no-op |
| 7 | Fear | 0073FF8C | 0x2C | 005D2960 | 005D30A0 | 005D3650 / 005D3680 |
| 10 | Follow | 0073D1C4 | 0x0C | 005D0A80 | 005D1E10 | 005D0AF0 / no-op |

  (Tamed, type 8, has its vtable outside this range.) The script's UM* commands (SSCCmd::Execute
  cases 0x24-0x31, 0xC7-0xCA) build these and add them with 005D5F60.
- **AIMoveOrder** (0x20, built on the stack by the modes): a point and a `CArray<AILook>`
  (vtable 0073D4B4, base 0073D460); `AILook` is {angle, turn time, wait, stop-first byte}.

## Functions rewritten

| Address | Name | Verification |
|---|---|---|
| 005D53B0 | AIBrain::Think (every AI tick) | ours only: corpse list empty, nothing seen, the mode in charge is in the list and referenced (skipped for an inactive controller) |
| 005D5DD0 | AIBrain::FindMode | pure |
| 005D5E30 | AIBrain::FindPatrolMode | pure |
| 005D5E60 | AIBrain::FindPlayerMode | pure |
| 005D5E90 | AIBrain::LeaderOrFollowed | pure |
| 005D5EE0 | AIBrain::PlayerModeLeader | pure |
| 005D5F20 | AIBrain::AggressionEnemy | pure |
| 005D50E0 | AIBrain::IsNearPath | pure (AL compared) |
| 005D5080 | AIBrain::SetOwner | writes the brain only: original on a copy, all 0x190 bytes and EAX compared |
| 005D58D0 | AIBrain::OnObjectDeleted | ours only: neither target nor seen is the object |
| 005D7520 | AIBrain::UpdateMove | ours only: action 0 or 2, looks shrink by at most one, a finished move holds no target |
| 005D76A0 | AIBrain::UpdateAttack | ours only: action 3 or 0, target unchanged |
| 005D7790 | AIBrain::StopAction | ours only: action 0, no target |
| 005D7B50 | AIBrain::AttackTarget | ours only: action 3, target = object |
| 005D7C60 | AIBrain::AttackWith | ours only: action 3 with the object, or unchanged |
| 005D7CE0 | AIBrain::UseOn | ours only: action 5 and no target (null object: unchanged) |
| 005D7AE0 | AIBrain::Walk | ours only: action 1 |
| 005D7B30 | AIBrain::TurnTowards | ours only: action 1 |
| 005D7DA0 | AIBrain::CastAt | ours only: action time = spell, action unchanged or 4 |
| 005D7E40 | AIBrain::Follow | ours only: action 6, target = object |
| 005D7E90 | AIBrain::SetSeen | ours only: seen = object, alerted in alert mode 1 |
| 005D0A80 | aimode::Priority50 (slot 4 of Guard, Sentry, Follow) | pure |
| 005D0AF0 | aimode::EnterIdle (slot 6 of Guard, Patrol, Sentry, Follow) | ours only (draws a random number): no target, action 0, not busy, decay in [0.8, 1.2] |
| 005D0A50 | aimode::ClearAction (slot 6 of CorpseWatcher, Player) | ours only: no target, action 0 |
| 005D0B70 | GuardMode::Set | writes the mode only: original on a copy, 0x20 bytes compared |
| 005D0BE0 | GuardMode::Execute | ours only: a busy brain is untouched, an idle one stays idle or starts a move (action 2, no target, no look time) |
| 005D0EB0 / 005D0F00 | GuardMode::Load / Save | load: original from the real stream into a copy (bytes recorded), ours from the recording, modes compared and all bytes consumed; save: both into capture streams, bytes and write sizes compared |
| 005D16B0 | PatrolMode::Evaluate | pure with points; without: ours only, -1000 and one point added |
| 005D1690 | PatrolMode::LastPoint | pure |
| 005D1700 | PatrolMode::Execute | ours only: as Guard, and the move goes to the current point |
| 005D1B50 | SentryMode::Set | original on a copy, 0x14 bytes compared |
| 005D1B70 | SentryMode::Execute | ours only: an idle brain starts a move |
| 005D1DA0 / 005D1DC0 | SentryMode::Load / Save | as Guard |
| 005D1E10 | FollowMode::Execute | ours only: an idle brain with a leader follows it |
| 005D1DE0 | FollowMode::OnObjectDeleted | ours only |
| 005D1E80 | FollowMode::Save | capture streams |
| 005D1E90 | SuspectionMode::Evaluate | without a seen object: original on a copy (0x18 bytes and result); with one (it may add to the memory): ours only, result = level |
| 005D1F80 | SuspectionMode::Execute | ours only: action unchanged or a new move without target |
| 005D21A0 | SuspectionMode::Enter | ours only: busy, action 2, no target |
| 005D2310 | SuspectionMode::Leave | ours only: not busy |
| 005D2330 / 005D2360 | SuspectionMode::Load / Save | as Guard |
| 005D2390 | AggressionMode::Evaluate | ours only: 10000 with chosen = enemy, or -1000 with nothing chosen |
| 005D2580 | AggressionMode::Execute | ours only: action unchanged, 3 or 4 |
| 005D26D0 / 005D2710 | AggressionMode::Enter / Leave | ours only |
| 005D27A0 | AggressionMode::Save | capture streams |
| 005D27D0 | CorpseWatcherMode::Evaluate | ours only: -1000, one corpse fewer |
| 005D3650 / 005D3680 | FearMode::Enter / Leave | ours only |
| 005D3690 / 005D36F0 | FearMode::Load / Save | as Guard |
| 005D37A0 | PlayerMode::OnObjectDeleted | ours only |
| 005D4A90 | PlayerMode::Execute (every tick for party units) | ours only: state in 0..2 (only ever reset to 0), a following brain has a target |
| 005D39E0 | PlayerMode::CommandWalk | ours only: state 1, action 1, no target, run flag stored |
| 005D3AE0 | PlayerMode::CommandGoTo | ours only: state 2, no target, z flag 1 with action 1 or z 0 |
| 005D3C30 | PlayerMode::CommandAttack | ours only |
| 005D3CF0 | PlayerMode::CommandUse | ours only |
| 005D3D20 / 005D3D90 | PlayerMode::CommandCast / CommandCastAt | ours only: state 0, action time = spell |
| 005D4DB0 | PlayerMode::Save | capture streams |

All registered addresses are vtable targets or direct-call targets, so the game's own mode objects
run these. None is in docs/rebirth-patched-functions.txt; the only patched callee is FatalError
(00480080), called through `game::FatalError` (an EI_ORIGINAL forwarder).

## Forwarders added (EI_ORIGINAL; the standalone build must replace these)

- Brain sub-objects: 005CFE40 / 005CF220 / 005D0030 (attention at +0xA4), 005CB700 / 005CBBE0 /
  005CB7F0 / 005CBC80 / 005CB260 / 005CC020 (memory at +0xD0), 005CC620 / 005CC560 (reports at
  +0x164), 005D8D50 (corpse list RemoveHead).
- Brain methods not rewritten: 005D6F90 UpdateFollow, 005D5B40 PlanLooks, 005D7F80
  FindStandPoint, 005D5990 CallAllies; PlayerMode 005D4170 UpdateCombat, 005D4860
  StaysWithParty, 005D37E0 PathClear; PatrolMode 005D12B0 AddPoint.
- Controller: 00555180 Idle, 00555200 StopQueued, 005546B0 Turn, 00554740 MoveTo, 00554830
  TurnTo, 00554B30 Attack, 00554D00 CastAt, 00554F50 Cast, 0054B820 CanCast, 0054C8B0
  CheckOrder, 00552780 Shout, 00555570 / 005553B0 / 00555480 / 00555630 the use variants,
  005AEA60 (voice at +0xB10) Say.
- Templates and helpers: 005573F0 / 005D8EE0 / 005D8B60 / 004CB770 / 005D9DE0 (CArray<AILook>
  construct, SetSize, Copy, destroy, GetAt), 00545B20 (move order reset), 0052B030 / 0052A8F0 /
  0050E220 / 0050E290 (counted reference reset/assign/load/save), 0052CEC0 (destruct counted
  references), 0040A3A0 (CRefObject::Release of modes), 004C96A0 (list constructor for
  Aggression's static candidate list at 007C54C8), 006E727B atexit, 006E79A0 memmove, 00593FC0
  (world: raise alarm), 006FC31D / 006FC0B0 / 00434FE0 (the console ostream's operator<< for
  text, char and manipulator).
- Called directly (rewritten elsewhere, not patched): CUnit::IsHostileTo (00530150),
  IsaacRandom::Next (004667F0), mfc::CString, mfc::PlexFreeDataChain.

## Original bugs and oddities kept on purpose

- Think's "new motivation" message prints an empty name for mode type 9 or anything above 10.
- AIBrain::IsNearPath sets only AL on its first "near" return (AH holds the FPU status); callers
  test AL. The clamp of the segment parameter keeps NaN (the FCOM masks select t when unordered).
- AIBrain::AttackTarget passes the player's order check (0054C8B0) an order whose point
  (+0x0C..+0x17) the original leaves uninitialised; ours zeroes it (see Risks).
- Guard's Execute counts failed searches for a place near its point; after 100 it also searches
  around the unit, on every tick until one search succeeds (only success resets the count).
- Patrol with no points calls FatalError "Unit %i has no patrol points, switching to guard" and
  adds a point at the unit instead (it does not switch to Guard).
- Suspection's Execute computes the "near the point" distance only while a move is under way;
  its level drops by 5 per tick near the point, by 50 in combat.
- Aggression's Execute asks for the current attack twice in a row (two 005CF220 calls with the
  same result).
- Player's CommandCast passes Cast a third argument whose upper bytes are the mode pointer's (only
  the low byte, the run flag, is read); ours passes the zero-extended flag.
- The Save functions write scalars from stack copies (`WriteCopy`), which only matters for the
  write addresses seen by a capturing stream.

## Skipped and why

- 005D0030 target choice (2.6 KB), 005D2960 / 005D30A0 Fear Evaluate / Execute (1.9 / 1.5 KB),
  005D4170 Player combat (1.2 KB), 005D6F90 UpdateFollow (1.4 KB; much x87 and a path search),
  005D5B40 PlanLooks, 005D7F80 FindStandPoint (path searches), 005D6630 / 005D6D20 alarm
  assembly: large float/path code; next candidates are 005D5B40 and 005D7F80 with an offline
  harness.
- 005D4860 StaysWithParty: walks the player's unit hash map through iterator helpers
  (00455830 / 00455890 / 004558B0) with an unclear frame.
- 005D5F60 AddMode, 005D4DF0 ClearModes, 005D4E80 / 005D4F70 brain reset/init, 005D1930 /
  005D1A40 Patrol Load / Save (Ghidra garbles their frames; the patrol point arrays are
  resized through template code), 005D0F90 / 005D2880 clones, destructors: allocation-heavy
  bookkeeping, rare.
- 005D1E60 Follow Load, 005D2770 Aggression Load, 005D4CD0 Player Load (loads a ScriptGroup for
  versions 0x46..0x5D): they load counted references by id; only run on game load.
- 005D7F40 (is the seen object an ally): leaves the caller's EAX upper bytes in the result.
- 005D1210 / 005D2920 (GetType of Patrol / Fear) and the other constant slots: trivial.
- 0x00480000-0x00490000: see above (error handling, DirectX, particles; no unit AI).

## Risks

- Everything except the lookups, IsNearPath, the setters and Load/Save is verified "ours only"
  with consistency checks, so a wrong branch that still leaves a consistent state would not be
  caught by the wrappers; the scenario (patrol back and forth, guard, sentry, follow, aggression)
  is the behavioural check.
- Think writes the "new motivation" line through the console ostream at 007C54E8 like the
  original; the scenario does not `expect` it because that stream may not be the in-game console.
- AttackTarget's order point: zero instead of stack garbage (only read, if at all, by 0054C8B0's
  deeper order code for attack orders).
- Floats: the original keeps intermediates on the FPU stack; ours uses `double` locals for those
  and `volatile float` where the original stores to a float (precise float model in both files).
  Equal under the game's single-precision control word except at exponent extremes.
- `AIMode::refs` is the 16-bit count the original increments with INC word; a 32-bit increment
  would corrupt the padding after it.
