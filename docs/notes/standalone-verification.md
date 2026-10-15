# Verifying the standalone exe (planned 2026-10-14; SV1 done)

The hook build checks a rewrite by running the original next to it (verify wrappers) and by comparing
whole runs (the harness suite). A standalone exe has no original to call, so the checks it runs have to be
recorded from the original beforehand. This note is the plan for that loop. It comes after cleanup and
before the standalone tickets (standalone.md).

## 1. The hook build stays the reference as long as it can

The standalone plan's first step (standalone.md section 2) keeps every shared class layout-compatible with the
exe and builds the hook DLL and the standalone exe from the same source. While that holds, every change still
goes through the hook build first: verify wrappers and the 152-run suite, exactly as today. The hook build is
retired only at the second step (standard containers, sizes change), and by then the recorded checks below
must cover what the wrappers covered.

## 2. Golden runs: whole-game behaviour recorded from the original

- **Repeatable runs first** (SV1, done). Harness runs use the virtual clock by default
  (tests/verify/harness_clock.h): only the main loop moves it (five world updates a frame), the main
  thread's sleeps and timed waits take no real time, other threads read it plus the real time since the
  last move (the new-game load waited on the music stream thread, which waits for the fader), sample
  ends are fired from the main loop on virtual time, the date and seeds are fixed, and the harness's
  steps run once a frame. `tools/compare.py --runs 3` on the vanilla suite: most scenarios repeat on
  both sides and match; the rest are in open-questions.md ("Harness runs that do not repeat").
- **What a golden holds**, per scenario, recorded with `--no-hooks` (the original exe alone):
  - the console output (as compare.py compares it today);
  - a state digest every N world ticks: a hash over every map object's position, vitals, effects, AI mode
    and target, the party records and the generator states, with a full dump of the same fields
    written on request. A mismatch then names the first tick and the object and field that differ;
  - frame captures at named points (compare_frames.py, with today's tolerance);
  - save files written at checkpoints, compared field by field after loading.
- **Where goldens live:** tests/golden/<scenario>/, regenerated only by an explicit command, with the reason
  in the commit. An intended change (a Rebirth fix behind its flag, the UTF-8 switch) re-records the
  affected goldens and says why; anything else that differs is a regression.
- **Both builds replay them.** The hook build checks the goldens too, so a golden that the hook build passes
  and the standalone exe fails points at the standalone change, not at the recording.

## 3. Function vectors: what scenarios reach rarely

Scenarios only see what happens to run. The verify wrappers check about 610 functions on every call. A
record mode in the hook build would write, for the functions whose inputs and outputs can be stored
(maths and vecmath, formulas, damage and stat tables, parsers, the LZ coder, pathing on a saved map),
a sample of real calls from the original as test vectors: the arguments, the object state they read, and
the result and state afterwards. A small test exe replays them against our code without the game.
Functions that need the whole world (AI ticks, the renderer) stay with the golden runs.

## 4. Coverage

A list of rewritten functions that no golden run and no vector reaches, from the hook build's call
counts. New scenarios target that list before the hook build is retired.

## 5. The loop for a standalone change

1. Build the hook DLL and the standalone exe from the same source.
2. Hook build: verify wrappers and the suite, as now (until it is retired).
3. Both builds: replay the golden runs (console, digests, frames, saves).
4. Test exe: the function vectors.

## Tickets (estimates, Opus)

| Ticket | Work | Size |
|---|---|---|
| SV1 | Deterministic runs that repeat: zone loading and the sound code's clocks on the virtual clock; every scenario passes `compare.py --runs 3` on both sides. Done 2026-10-14: virtual clock by default; what does not repeat yet is in open-questions.md | 1-2 |
| SV2 | State digest and dump in the harness; golden record / compare commands; tests/golden layout | 1 |
| SV3 | Record goldens for the suite (vanilla, Rebirth flags on and off); saves at checkpoints | 1 |
| SV4 | Function vector record mode and the replay test exe, for the families listed in section 3 | 2 |
| SV5 | Coverage list, and scenarios for what it shows uncovered | 1+ |

Multiplayer is not covered: the harness starts only the lobby. A two-client harness (also wanted for the
Rebirth multiplayer patches, rebirth.md) would add network runs to the goldens.

## SV2: the state digest and the golden tooling (2026-10-15)

**The digest** (tests/verify/harness_digest.h/.cpp, verify build). A record is the world server's state as
named fields, read from the shared layouts without changing anything:
- `world`: tick, update count, pause, the clock (time base, seconds per tick), wind, night level, weather,
  the five alarms, climate / cave, the zone flags, start position, session kind, the counts of every id map,
  missiles, counted objects and shared objects, both diplomacy tables;
- `random`: the ISAAC generator (count, a, b, c, hashes of its results and memory) and the CRT's rand()
  state of the main thread (`_getptd()+0x14`, 006F00D1);
- `player<id>`: the world player's numbers and its party: money, items, members' unit ids, the variables
  ("var.<name>", sorted by name);
- `obj<id>`, every object of the server's `objects_` (by id): class, flags, type, prototype; for map
  objects (those in `mapObjects_`, `sentObjects_`, `otherMapObjects_`) position, orientation, scale,
  update countdown, animation speed, sector cell, every effect, and the server half's hit points and mana;
  for units the owner (player id), side, relations, death, animation, movement modes, weapons, the motion
  (points, segment, speed), the controller (state, busy, combat, commands and targets by id, experience,
  speed, move factors) and the AI brain (action, point, look time, target and seen by id, alertness,
  alarm, decay, noticed point and level, list counts, the mode in charge and the mode list by name).
No pointer is written: objects by id, AI modes by name (their game.exe vtable when not known).

`digest.txt` holds one record every N world updates (scenario step `digest <N>`, or `EI_DIGEST=<N>` for a
whole run; taken after the main loop's server update, which the harness already wraps), plus one per
`digest` step without a period. Each record is a header (record number, world update, frame, tick, zone,
hash and the four section hashes) and the fields that changed since the record before; the first lists
everything, so any record's whole state can be rebuilt. `dump <name>` writes the full state now to
`dump_<name>.txt`. With `randtrace` each record is also a note in randtrace.txt, so a digest difference and
the random draws around it line up. Size: zone1 has about 780 world objects (48 units); the first record
is about 0.5 MB, later ones a few KB each (moving units, the generator, regeneration counters).

**The tool** (tools/golden.py): `diff A B` walks two runs' digests record by record and prints, at the first
record whose state differs, every differing field in record order (world, random, players, objects by
id), e.g. `obj1804 (80 unhuma "") pos.x: 65.1067123 in A, 65.1067162 in B`; `--ulps N` lets floats differ
by N units in the last place, `--ignore` drops fields, `--records K` shows more records. `state A --record N`
prints a rebuilt state. `record` keeps a passing `--no-hooks` run with a digest as
`tests/golden/<scenario>/<install>/` (console.txt, digest.txt, golden.json: date, commit, digest period,
reason); `compare` checks a run against it (console, then the digests). golden.py starts no games: runs
go through the scheduler (`EI_DIGEST=10` in the environment reaches the games of `work.py test`).

For SV1a / SV1b: put `digest 1` (or `EI_DIGEST=1`) and `randtrace` on the scenario, run the two sides,
then `golden.py diff` names the first update and field where they part.

Left out on purpose: a unit's motion floats while it has no trajectory (never written before the first
path; zero in a run of the original alone, heap leftovers with our hooks), and pointers. `golden.py`
leaves `updateCountdown` out of comparisons unless `--all-fields`: it follows the unit's heap address
(open-questions.md, "Harness runs that do not repeat"), so even two runs of the original differ there.
Not in the digest yet: the client world, missiles' own state (only their count), timed objects' own
state (class, flags, type only), the script queues and countdowns, the AI event table.

First results (harness_digest, 2026-10-15): the original repeats itself and ours repeats itself in every
other field; original against ours agree for 225 world updates, then only `random crt.rand` differs (an
animation pick one update earlier in ours; open-questions.md). Note: `work.py test` drops run_all's
`--clock`; set `EI_HARNESS_CLOCK=virtual,randtrace` in the environment instead.

## SV1a: the verify build's wolf fight (2026-10-15)

units and sandbox_actions: ours killed the wolf after 400 ms of virtual time, the original after 700 ms.
Steps that found it, in order (worth repeating for the next such case):

1. `EI_PLAIN=1` (no verify wrappers) against `--no-hooks`, with `randtrace`: the same step frames, the
   same console, 700 ms. So the rewrite was exact; the verify build was not.
2. Random traces of the verify build against the plain one: the first draw that differs (not only its
   caller: the wrappers run originals, so callers differ everywhere) was an extra rand() in
   AIViewer::Refresh (a sight countdown run out early); before it, a third animation pick
   (FindAnimation 005301B0) in one unit's update where the plain build made two.
3. A bisection over the ~5700 wrappers through builderifyerified.txt (a listed function runs
   its plain replacement): enable half, see whether the fight ends at 400 ms; a run that crashes
   (some wrappers call each other's trampolines) splits its half again. 13 runs named 0052EF90.

Cause: `UnitReadNetStateVerify` (tests/verify/verify_net_state.cpp, ReadChecked) lets ours read a
unit's delta state, puts the state, stream and delta globals back and has the original read it again.
The random generators were not put back, so the animation field's restart (slot 14, 005352B0 ->
005301B0) drew rand() again. Putting them back is not enough either: ours' change-driven reactions
(skipped in the re-read) may draw first, and the re-read then picks its animation from other values
and leaves that pick on the unit. Fix: a read in which ours draws from either generator runs ours
only; otherwise the generators are put back for the original's re-read, compared (it must not draw
either) and set to ours' state. Checked: units, sandbox_actions and harness_digest against the
original with `EI_DIGEST=5` and `golden.py diff`: every record the same (but the fields left out on
purpose), the same console; Unit::ReadNetState 0 mismatches.

Other wrappers that run an original after ours on live objects can do the same (the generators,
or other shared state the original's run changes again); the plain-versus-verify trace finds them.

## SV1f / SV1h: the virtual clock's speed and the other threads' clock (2026-10-15)

Two causes, not one.

- SV1h (other threads stand still after a long main-thread block): other threads read the virtual
  time plus the real time since the main thread last moved it, never going back. A load or a movie
  (seconds of real time with no advance) put their clock that far ahead; from the next frame they
  saw `virtual + a few ms`, below what they had seen, so their clock stood still until the frames
  caught up (each frame takes back only its own advance, and none on a busy machine whose frames take
  longer than they advance). The cursor thread's 41 ms steps took 1.5 s (render_cursor_present: 2
  presents in a 1.6 s in-game stall), and the main thread's music waits (WaitAndEndAfter: Sleep(0)
  until the stream thread's fader, on the other threads' clock, has faded out) waited as long again.
  Now their clock runs from an anchor (their time, the real time it was taken at) that each main-thread
  advance moves to max(their time then, the new virtual time): real time when the main thread stands
  still, the virtual time when it runs ahead, never back and never standing still. Freeze holds it.
  render_cursor_present runs on the virtual clock again (28 presents in the stall). The main thread's
  clock is untouched, so nothing a digest sees changes (other threads' clocks drive only the music
  fader and the cursor thread's steps).
- SV1f (sandbox_map 72-81 s against 17 s): `speed 10`. On the virtual clock a frame advances
  5 x the update length, so the main loop runs five updates a frame at any speed; a 10 ms update only
  gives each frame 50 ms of virtual time instead of 275. zone1's intro blocks the leader until its
  speech ends (about 36.5 s of samples, ended on virtual time), so it took 5.5 times the frames: at
  about 9 frames a second on a busy machine, 67-80 s. On real time the run passed by racing: the
  `waitfor Not(IsUnitBlocked(...))` was true after 0 ms, before the intro had blocked the leader, so
  it never waited for the intro at all. sandbox_map now runs without `speed` on the virtual clock:
  26 s on a busy machine, the intro 36.85 s of virtual time in 12 s. Measured in one batch on the
  same build: with `speed 10` 81 s (timeout), without 26 s. The 78 other scenarios with `speed 10`
  keep it (their goldens are being recorded with it); SV1i looks at which of them wait on sound or
  timers.
