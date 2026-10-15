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
| SV3 | Record goldens for the suite (vanilla, Rebirth flags on and off); saves at checkpoints. Done 2026-10-15: 43 goldens, `--golden` in run_all and work.py test (section SV3) | 1 |
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

## SV3: the suite's goldens (2026-10-15)

**What a golden is** (tests/golden/<scenario>/<install>/, tools/golden.py): golden.json (date, commit, digest period,
the fields left out and kept in clear, how many runs agreed, the reason), console.txt, digest.txt and dump_*.txt in
**compact form**, and saves.txt when the saves repeat (below). The compact form is the digest's own record format with
the world, the generators and the players field by field and every object as hashes of its fields (a unit: one hash
each for `#place` (position, orientation, scale, cell, motion), `#vitals` (hit points, mana, regeneration, effects,
death), `#ai` and `#unit`, the rest; other objects one `#`). A golden is 40-90 KB (the first record, ~780 objects, is
most of it); the 43 goldens take 2.2 MB. A difference names the record, the object and the group; for the fields
behind a hash, run the original again (its runs repeat) and `golden.py diff` the two full digests.

**Saves at checkpoints:** the seven scenarios that load a save (ground_prints, levers, magic_traps, script_copy,
script_fx, terrain_water_load, world_load) take `dump loaded_<save>` once the load is done: the loaded world field by
field, kept as a compact dump. The saves' bytes are kept (saves.txt: size and hash of every `<save>/*.sav` but the
thumbnail shot.sav) only when every run wrote the same bytes, and that never happened: mission.sav differs in size and
bytes between two runs of the original in every scenario that saves (SV3b).

**Known differences** (golden.py KNOWN, each with its ticket): fields kept in clear in compact digests and left out
of comparisons with a golden (`compare --strict` keeps them), so a golden needs no re-recording when the ticket lands;
the entry is dropped then. One so far: SV3a, the main menu world's unit 2 (unmoco2), whose hit points ours leaves
unwritten (every hooked run's first record).

**Recording** (period 20 world updates, `run_all.GOLDEN_PERIOD`):
```
set EI_DIGEST=20 & python tools\work.py test <ID> --no-hooks        (the original; twice)
set EI_DIGEST=20 & python tools\work.py test <ID>                   (ours; twice)
python tools\golden.py snapshot <scratch>\orig1                       (after each suite: orig1, orig2, ours1, ours2)
python tools\golden.py record --install vanilla --runs <scratch>\orig1 <scratch>\orig2 <scratch>\ours1 <scratch>\ours2 --reason "..."
python tools\golden.py record --install rebirth --runs ...            (the same folders)
```
`record` keeps a scenario only when the second run of the original and both of ours agree with the first run of the
original in the console, every digest record and every dump (but the fields left out and the known differences).
Without our hooks render_present_fullscreen is not run at all (it would switch the display mode; run_all.plan), and
rebirth_flags (vanilla), render_device_lost and render_present_shot fail.

**Checking:** `python tools\work.py test <ID> --golden [run_all args]` (or `run_all.py --golden`) runs the games with
EI_DIGEST=20 and then compares every run that has a golden with it (golden.py compare): "goldens: N the same, M
differ, ..." and GOLDENS THE SAME / GOLDENS DIFFER, which also sets the exit code. Native runs compare with the vanilla
golden. The comparison runs in the `work.py test` process, after the batch, so the scheduler needs nothing new.
Rebirth with the flags on is the default Rebirth run (FollowAddon: every flag follows what addon.dll applies) against
the Rebirth golden (addon's code alone); with the flags off (`set EI_REBIRTH=FollowAddon=0`) the rewrites of what
addon.dll patches are not installed, so addon's code runs there and the same Rebirth golden applies.

**Covered (43):** vanilla 38: camera, console, harness_digest, hero_death, items, levers, net_lobby, objects_more,
party_records, render_movie, render_movie_present, render_scene, render_terrain, render_titles_present,
sandbox_actions, script_copy, script_parse, script_quest, smoke, spells_cast, ui_battle_spells, ui_container_input,
ui_help, ui_hero_delete, ui_hero_screen, ui_hero_stats, ui_main_menu, ui_objectives, ui_options_bind, ui_order_move,
ui_quest_notice, ui_saveload, ui_saveload_delete, ui_screens, ui_trade_restore, ui_widescreen, unit_create,
world_load. Rebirth 5: console, harness_digest, items, script_parse, ui_help. Rebirth with the flags off: FLAGSOFF.

**Not covered yet**, by the first run that differed from the first run of the original (`@N`: world update of the
first differing record, then the fields; unmoto: the zone's two idle wolves, obj1936/1937):
- *The original does not repeat* (46; the update-map countdown from heap addresses, SV1b, is the likely cause of
  nearly all: idle units that think on other updates, so the generators' counts part): combat_more (console line 57);
  effects @80 isaac.count; ground_prints @960 flags (hero); map_objects @560 isaac.count, ai.standCooldown (unmoto);
  models @540 isaac.count, crt.rand; movement @540 isaac.count, ai.standCooldown (unmoto); particles @80 isaac.count,
  ai.standCooldown (unmoto); render_present @280 isaac.count, ai.standCooldown (unmoto); sandbox_map (console line 4);
  script_exec @560 crt.rand, regenTicks (unmoto); script_fx @60 isaac.count, ai.standCooldown (unmoto); spells @540
  updateMap, isaac.count; spells_hero @620 isaac.count, regenTicks (unmoto); trader_stock @200 isaac.count;
  ui_base_dialogue @100 isaac.count; ui_camp @580 isaac.count; ui_global_map_pick @320 isaac.count, crt.rand; ui_hud
  @540 isaac.count; ui_mp_host @520 isaac.count; ui_trade_hold @100 isaac.count; ui_view_screen @220 isaac.count,
  crt.rand; ui_widescreen_hud @280 isaac.count, crt.rand; ui_widescreen_more @660 isaac.count, regenTicks (wolf
  obj1890); ui_widescreen_screens @380 updateMap, isaac.count; unit_ai @540 isaac.count; units @560 isaac.count;
  world @280 isaac.count, ai.standCooldown (unmoto); world_client @560 isaac.count, regenTicks (unmoto);
  zone_base_bz1g @100 isaac.count, crt.rand; zone_gz2g_traps @100 isaac.count; zone_gz3g @180 crt.rand. Rebirth:
  effects_bow @320, frames_basic @200 updateMap, ground_prints @320, levers @320, map_objects @280, rebirth_flags
  (console line 38: a group's unit order), trader_stock @300, ui_camp @580 flags (unmoto), ui_help_page @380, ui_hud
  @460 ai.point (hero), ui_mp_host @500 (the hero's name: tteesstt / test), ui_trade_restore @100,
  ui_widescreen_hud @440 pos.x (unmoto), ui_widescreen_more @900 updateMap, ui_widescreen_screens @1320; all with
  isaac.count first but where named.
- *Ours repeats but differs from the original, which repeated* (14; the same in ours1 and ours2, so not noise: the
  heap layout with our hooks, SV1b, or a verify wrapper's side effect, SV1g): combat (console line 61, a hit);
  effects_bow @120 isaac.count; frames_basic @280 and render_cpuwindows @280 isaac.count, ai.standCooldown (unmoto);
  magic_traps @100 crt.rand; sound (console line 19: which wolf the hero sees); ui_battle_commands @260 ai.noticed
  (hero); ui_camp_deals @140 isaac.count; ui_describe @180 and ui_help_page @380 isaac.count, ai.standCooldown
  (unmoto); ui_quick_items @120 isaac.count; unit_ai_fear @100 isaac.count. Rebirth: ui_camp_deals @140,
  ui_quick_items @120, ui_trade_hold @100, all isaac.count first.
- *Ours does not repeat* (2): render_present_cpuwindows @260 isaac.count, terrain_water_load @340 crt.rand.
- *No golden possible without our hooks:* rebirth_flags (vanilla; its flags), render_device_lost, render_present_shot,
  render_present_fullscreen; render_present_window fails too and, like the three watchdog_* scenarios, never runs a
  world update, so it writes no digest.

When SV1b / SV1g land, run the four suites again and `record`: a scenario that repeats then gets its golden, and one
that stops repeating is reported, not recorded.
