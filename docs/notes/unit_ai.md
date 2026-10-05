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
| Load / Save of Guard, Sentry, Suspection, Fear (Save only: Follow, Aggression, Player) | slots 11 / 12 | load: original reads the real stream into a copy (bytes recorded), ours from the recording, all bytes consumed; save: both into capture streams, bytes and write sizes compared |

None is in `docs/rebirth-patched-functions.txt`; the only patched callee is FatalError, called
through `game::FatalError` (EI_ORIGINAL). Forwarders are in `unit_ai.h`, `unit_ai_modes.cpp`,
`originals_ai.h`; the standalone build must replace them.

## Original bugs and oddities kept on purpose

- Think's "new motivation" message prints an empty name for mode type 9 or above 10.
- `IsNearPath` sets only AL on its first "near" return (AH holds the FPU status); callers test AL.
  The clamp of the segment parameter keeps NaN.
- `AttackTarget` passes the player's order check (0054C8B0) an order whose point (+0x0C..+0x17)
  the original leaves uninitialised; ours zeroes it.
- Guard's Execute counts failed searches near its point; after 100 it also searches around the unit,
  on every tick until one search succeeds (only success resets the count).
- Patrol with no points calls FatalError "Unit %i has no patrol points, switching to guard" and
  adds a point at the unit (it does not switch to Guard).
- Suspection's Execute computes the "near the point" distance only while a move is under way; its
  level drops by 5 per tick near the point, by 50 in combat.
- Aggression's Execute asks for the current attack twice in a row (two 005CF220 calls, same result).
- Player's CommandCast passes Cast a third argument whose upper bytes are the mode pointer's (only
  the low byte, the run flag, is read); ours passes the zero-extended flag.
- Save functions write scalars from stack copies (`WriteCopy`), which only matters to a capturing
  stream.

## Skipped and why

- Large float/path code: 005D0030 target choice (2.6 KB), 005D2960 / 005D30A0 Fear Evaluate /
  Execute, 005D4170 Player combat, 005D6F90 UpdateFollow, 005D5B40 PlanLooks, 005D7F80
  FindStandPoint, 005D6630 / 005D6D20 alarm assembly. Next candidates: 005D5B40 and 005D7F80 with
  an offline harness.
- 005D4860 StaysWithParty: walks the player's unit hash map through iterator helpers (00455830 /
  00455890 / 004558B0) with an unclear frame.
- 005D5F60 AddMode, 005D4DF0 ClearModes, 005D4E80 / 005D4F70 reset/init, 005D1930 / 005D1A40 Patrol
  Load / Save (Ghidra garbles the frames; arrays resized through template code), 005D0F90 /
  005D2880 clones, destructors: allocation-heavy bookkeeping, rare.
- 005D1E60 Follow Load, 005D2770 Aggression Load, 005D4CD0 Player Load (loads a ScriptGroup for
  versions 0x46..0x5D): counted references by id; only on game load.
- 005D7F40 (seen object is an ally): leaves the caller's EAX upper bytes in the result.
- 005D1210 / 005D2920 (GetType of Patrol / Fear) and other constant slots: trivial.

## Risks

- Most are verified "ours only" with consistency checks: a wrong branch that leaves a consistent
  state would pass; the scenario (patrol, guard, sentry, follow, aggression) is the behavioural check.
- Think writes the "new motivation" line through the console ostream 007C54E8 like the original; the
  scenario does not `expect` it (the stream may not be the in-game console).
- Floats: `double` locals for values the original keeps on the FPU stack, `volatile float` where it
  stores to a float; equal under the game's single-precision control word except at exponent
  extremes.
- `AIMode::refs` is a 16-bit count (INC word); a 32-bit increment would corrupt the padding.
