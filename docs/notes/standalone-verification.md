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
counts. New scenarios target that list before the hook build is retired. Done in SV5: `tests/coverage/coverage.csv`
(`python tools\coverage.py`).

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
| SV3 | Record goldens for the suite (vanilla, Rebirth flags on and off); saves at checkpoints. Done 2026-10-15: 42 goldens, `--golden` in run_all and work.py test (section SV3) | 1 |
| SV4 | Function vector record mode and the replay test exe, for the families listed in section 3 | 2 |
| SV5 | Coverage list, and scenarios for what it shows uncovered. Done 2026-10-15: `EI_COVER=hooks`, tools/coverage.py, tests/coverage; five scenarios; the rest in SV5a-d (section SV5) | 1+ |

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
Digest 2 (SV2a, below) adds the client world, missiles, timed objects, the script and the AI event table.

## SV2a: digest 2, the groups beyond the server world (2026-10-15)

New entity groups, each in its own keys so that the server world's entities (and so the goldens' hashes) stay
as they were. In record order after the objects:
- `timed<id>`: every timed object of the server (`timedObjects_`) that is an effect (world_effect.h; class
  0x48, 0x49, 0x4A, 0x4B, 0x4D, 0x4E, 0x5A, 0x5B, 0x5E, 99, 0x65): position, velocity, moving, start tick,
  then its class's fields (sound: volume, radius, looping; particle source / wall: type, size, attached
  object, target point, unit; point light: colour, range; lightning: ends and their objects; missile
  0x4E / 0x5B: arrived, target id). Other timed objects keep only their `obj<id>` fields.
- `missile<n>`: the server's missile list (`projectiles_`) by place. "Projectile" is CSpellEffect (spell.h,
  0xA8 bytes; 0058E670 builds one per cast): the spell (its prototype index in the description), state,
  flags, position, caster and target by id, target point, timer, damage, radius, target count, and the
  ids of its visual (the flying CEffectArrow: its place is in `timed<visual>`), sound and light. A list
  entry with another vtable is described by it and has no fields.
- `aievents`: the AI event table (CWorldServer + 0x2838): its tick, grid size, the event count and per
  side a count and a hash of the events in wheel order (place, origin, expiry, source, side; not the
  payload, which 005C9630 leaves as the free list's or the stack's leftovers). No run so far had an event:
  Ghidra shows no caller of the creator 005C9630, so the table may stay empty in this version.
- `script`: the mission script (CScript 007C27D4): loaded, file count, the next automatic ids, each
  running instance (killed flag, name and variables as hashes), each waiting run (the command queue:
  block id, next command, ticks, sleep start, its instance, variables hash), the global variables by name
  (`global.<name>` floats, `gstring.` / `gobject.` / `ggroup.` as text hash, id and sorted-id hash; by
  index past the names), the FX sources (id, sound, place, idle; a randomized source's countdown, delays
  and spread: CreateFXSource's new sources hold the original's stack leftovers there, docs/notes/script.md,
  so those are not read), particle sources (moves, lifetime), point lights in use, lightning ids.
- `client`: the client world (CWorldClient 007AF5B8): tick, pause, Segment count, tick fraction, party
  danger, object, unit and synchronised counts; `cunit<id>`: each of its units (same ids as the server's):
  place, orientation, animation entries and start, animation speed, death, packed vitals, effect count,
  controller class.

The title line says `# ei state digest 2; groups timed,missile,aievents,script,client,cunit; ...`, dumps get a
title too, and the header has a hash per new section. tools/golden.py reads the groups (`Digest.groups()`; a
digest without the list has the four of digest 1): a golden's comparison compacts the run down to the groups the
golden has, and `diff` of digests with different groups compares the ones both have (and says which it left out).
So the 42 goldens of digest 1 still compare (checked: 8 goldens the same with the new digest), and only a new
recording brings the groups in. In compact form `aievents` and `client` stay field by field; `script` becomes one
hash per kind (`#run`, `#inst`, `#global`, `#gobject`, `#fx`, ...), `cunit` units as the server's (`#place`,
`#vitals`, `#unit`), the rest one `#`.

Known differences (KNOWN): `cunit2` (the main menu's unmoco2) hp / maxHp / mana / maxMana follow the
unwritten server vitals (SV3a; ours maxHp 20, mana 20; the original 1 and 0); `script gobject.i` (SV2b).

Checked (harness_digest, spells_cast, script_quest, levers, combat, script_fx, ui_battle_spells; both installs;
two runs of ours and one fresh run of the original, EI_DIGEST=20): every new group is the same in ours' two runs
and against the original, but for SV2b: under Rebirth with our hooks zone1's script variable `i` (Rebirth's
script; the last object of an address-ordered group) is unit 702 in some runs and 1884 in others, the original's
702 in every run, so some allocation still moves between our Rebirth runs. script_fx vanilla parts from the
original at update 60 in `isaac.count` (as in SV3's list), before any new group differs. What the new groups hold
in these runs: 6-13 timed effects and 11-15 missiles over a run, the script's 25 instances and its runs, 51 client
objects in the main menu; the AI event table was empty in all of them.

Full suite with `--golden` (117 runs): all pass, 41 of 42 goldens the same; unit_create parted once at update 80
in `crt.rand` only (no new group differed either), and matched its golden in two runs alone (SV2c).

**The reference cache:** `run_all` takes a `--no-hooks` run from the shared reference cache (build/reference),
whose key leaves out the harness. After a digest change, run the original with `--fresh`, or the run's digest is
the one of whichever harness filled the cache (an older digest: golden.py then compares the groups both have).
Record goldens with `--fresh`.

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

## SV1g: other wrappers that take the verify build off course (2026-10-15)

Tools (kept):
- Every line of the random trace (`EI_HARNESS_CLOCK=virtual,randtrace`) carries the generator's
  position (`#<n>`: ISAAC refills * 256 + index, rand()'s seed), and its notes now show every move of
  the virtual clock (`clock frame time`, sleeps, timed waits) and every sound stopped or ended by the
  game, so two runs show where their virtual times part.
- `tools/randtrace.py A B` walks A's main-thread draws per generator and reports the first one B
  draws in no frame or another frame (wrapper replays draw the same positions again, from other
  callers, which it ignores); `--kept B [A]` lists original (game.exe) draws whose result stayed.
- Bisection: copies of build\verify (`build\bisectN`, each with its own verified.txt), run through
  `work.py test` with `EI_BUILD_DIR`, judged with randtrace.py. 00412720 (RefDestruct1Verify) must
  stay a wrapper in every part: RefValueDeleteVerify (00412700 and its copies) calls its trampoline.

Found and fixed: AIAttention::Choose (005D0030) and CanReach (005CF3C0). The wrappers ran the
original on the live brain, then ours, without putting the generators back: the reach test's order
check plans the attack, and BuildAttackSteps' blow height draws ISAAC, so every choice drew twice.
unit_ai_fear's foe drew three blow heights where the plain build drew one (ISAAC frame 16), and
magic_traps parted at rand frame 18. Both wrappers now save the generators, put them back after the
original and check that ours ends where the original did. After the fix unit_ai_fear, magic_traps and
sound made every draw of the plain build in the same frame (Choose: 0 mismatches).

A full vanilla sweep (plain against verify, both with randtrace) after the fix, by the first difference:
- The same draws in the same frames: camera, console, frames_basic, ground_prints, harness_digest,
  hero_death, items, levers, net_lobby, party_records, the render_* scenarios, sandbox_actions,
  script_copy / parse / quest, smoke, spells_cast, most ui_* screens, unit_ai_fear, watchdog_*,
  world_load. ISAAC only: unit_create, zone_gz3g, ui_base_dialogue, magic_traps, terrain_water_load.
- Frames ~105-111 (combat, combat_more, map_objects, models, movement, sandbox_map, spells, units,
  unit_ai, ui_hud, ui_widescreen_more / _screens, world_client): the update-map countdown at world
  update ~530-555 (CMapObject +0x174 from bits of the object's address), SV1b's heap question.
- Terrain::PlayAmbience drawing in one build and not the other (ui_view_screen, ui_trade_hold(_restore),
  trader_stock, ui_camp(_deals), ui_describe, ui_global_map_pick, world, world_client, spells_hero,
  script_exec, effects_bow, render_device_lost, ui_quick_items): not a wrapper. In ui_view_screen the
  `screen 7` step's switch (it stops the playing sounds) happened inside the step at 1001500 in the
  verify build and one frame later at 1001550 in the plain one. The bisection over all wrappers did
  not converge: a set of 22 effect wrappers turned it on, none of its halves or quarters did, and the
  same 22 then ran like plain once. The frame the server's answer lands in depends on real time
  (follow-up SV1j); the verify build's extra work only makes the early answer likelier.

## SV1j: a pending close-all and real window messages (2026-10-15)

Not the server's answer: 00578B00 / 00578B20 set the interface's close-all flag (007C7D94 is
CInterface3D +0x44, world_glue's `CloseAllRequested`) before the message goes out. The windows close,
and the playing sounds stop (CBattleScreenSetup::Close -> CSoundManager::StopAll), at whichever comes
first: the input dispatcher's first step (CloseAllPending 005F5AB0) for the next message WinMain's loop
takes, or the next pass (the answer's CreateScreen -> Activate, else Draw). A step runs in the
after-frame PeekMessageA, so whether a real message (Miles' WM_TIMER, WM_PAINT) waits right after it is
real time; the verify build, slower, nearly always had one. The same holds for a click that asks for
a screen. Stacks from the random trace's new caller list on "sound ended by the game" notes (the
calls found by scanning the stack, game.exe addresses and ei_hook+offset; resolve with ei_hook.map).

Harness fix (harness.cpp HoldMessagesForCloseAll): on the virtual clock, while the flag is set, the
main loop's peeks report no message, so the next pass always does the close; after two frames
(no pass took it up) messages pass again. After it ui_view_screen and ui_trade_hold, trader_stock,
ui_camp, ui_camp_deals, ui_describe, ui_global_map_pick, world, world_client, spells_hero,
script_exec, effects_bow, ui_quick_items, ui_trade_restore, render_device_lost make every draw in the
same frame plain against verify and plain against plain (two runs each, vanilla).
- Other early ones, not bisected: effects (frame 12), objects_more (CLightning::Start, 12), script_fx
  (8), ui_mp_host (96, SaveParty), unit_create's rand (12: protection checks against the animation
  pick order; the bisection blamed every part), zone_base_bz1g, zone_gz2g_traps. Several of these also
  differ plain against plain (effects, trader_stock, ui_quick_items, effects_bow, zone_base_bz1g,
  ui_base_dialogue, zone_gz2g_traps), so the trace needs repeat runs before a bisection.

Not a divergence found yet, but one to keep in mind: CheckNear wrappers (Score, Distance2, ...) return
the original's value in the verify build, so where ours differs by a few ULPs the verify build follows
the original's numbers and the plain build ours.

## SV3: the suite's goldens (2026-10-15)

**What a golden is** (tests/golden/<scenario>/<install>/, tools/golden.py): golden.json (date, commit, digest period,
the fields left out and kept in clear, how many runs agreed, the reason), console.txt, digest.txt and dump_*.txt in
**compact form**, and saves.txt when the saves repeat (below). The compact form is the digest's own record format with
the world, the generators and the players field by field and every object as hashes of its fields (a unit: one hash
each for `#place` (position, orientation, scale, cell, motion), `#vitals` (hit points, mana, regeneration, effects,
death), `#ai` and `#unit`, the rest; other objects one `#`). A golden is 40-90 KB (the first record, ~780 objects, is
most of it); the 42 goldens take 2.2 MB. A difference names the record, the object and the group; for the fields
behind a hash, run the original again (its runs repeat) and `golden.py diff` the two full digests.

**Saves at checkpoints:** the seven scenarios that load a save (ground_prints, levers, magic_traps, script_copy,
script_fx, terrain_water_load, world_load) take `dump loaded_<save>` once the load is done: the loaded world field by
field, kept as a compact dump. The saves' bytes are kept (saves.txt: size and hash of every `<save>/*.sav` but the
thumbnail shot.sav) only when every run wrote the same bytes, and that never happened: mission.sav differs in size and
bytes between two runs of the original in every scenario that saves (SV3b).

**Known differences** (golden.py KNOWN, each with its ticket): fields kept in clear in compact digests and left out
of comparisons with a golden (`compare --strict` keeps them), so a golden needs no re-recording when the ticket lands;
the entry is dropped then. None now: SV3a (the main menu unit unmoco2's hit points, every hooked run's first record)
was CMapObjectManager::GiveDefaults' stack copy, whose fields Reset and SetDefaults leave alone (the slots' shares
and factors, fixedMaxHitPoints) held our DLL's addresses and -1 instead of the original's tiny leftovers; ours now
seeds them with the original's values there (map_object_manager.cpp).

**Recording** (period 20 world updates, `run_all.GOLDEN_PERIOD`):
```
set EI_DIGEST=20 & python tools\work.py test <ID> --no-hooks --fresh (the original; twice; --fresh: SV2a)
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

**Covered (42; superseded by SV3d's re-recording below):** vanilla 37: camera, console, harness_digest, hero_death, items, levers, net_lobby, objects_more,
party_records, render_movie, render_movie_present, render_scene, render_terrain, render_titles_present,
sandbox_actions, script_copy, script_parse, script_quest, smoke, spells_cast, ui_battle_spells, ui_container_input,
ui_help, ui_hero_delete, ui_hero_screen, ui_hero_stats, ui_main_menu, ui_objectives, ui_options_bind, ui_order_move,
ui_quest_notice, ui_saveload, ui_saveload_delete, ui_screens, ui_trade_restore, unit_create, world_load. Rebirth 5: console, harness_digest, items, script_parse, ui_help. Rebirth with the flags off
(`set EI_REBIRTH=FollowAddon=0`): all ten Rebirth goldens hold too since SV3c (below).

**Speed 10:** 28 of the 37 vanilla goldens' scenarios (and 4 of the 5 Rebirth ones) have `speed 10`, whose handling on the virtual clock SV1i
changes; their goldens are recorded with the old handling and need recording again after SV1i (the same four suites
and `record`).

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
- *Ours does not repeat* (3): render_present_cpuwindows @260 isaac.count, terrain_water_load @340 crt.rand;
  ui_widescreen (recorded, then dropped: after main's R10, our renderer by default, one of two runs took one more
  frame by world update 600, isaac.count).
- *No golden possible without our hooks:* rebirth_flags (vanilla; its flags), render_device_lost, render_present_shot,
  render_present_fullscreen; render_present_window fails too and, like the three watchdog_* scenarios, never runs a
  world update, so it writes no digest.

When SV1b / SV1g land, run the four suites again and `record`: a scenario that repeats then gets its golden, and one
that stops repeating is reported, not recorded.

## SV4: function vectors, record mode and replay (2026-10-15)

**Record mode** (verify build, `EI_VECTORS=1` in the game's environment; `work.py test` passes it on). A
family's recorders (`EI_RECORD(addr, recorder, stolen, orig)` in tests/vectors/record_<family>.cpp, stolen
bytes as for EI_VERIFY, checked by check_stolen.py and check_signatures.py) replace every other hook at
their address, ours and the verify wrapper alike, so the game runs the original there. A recorder copies
the arguments and the state the function reads, reads the x87 control word, calls the original, copies
the result and the state after, and offers that payload (a plain struct, tests/vectors/<family>_vectors.h)
to its `Sampler`. The sampler drops repeats (a hash of control word and payload) and keeps a reservoir of
`cap` distinct calls per bucket with a fixed-seed generator, so the same calls give the same sample; a
bucket keeps a rare path from drowning among common calls (ISAAC's refilling draw). The harness's report
writes `<run>/vectors/<family>/<function>.vec`.

**Data** (tests/vectors/data/<family>/<function>.vec, checked in). `python tools/vectors.py merge <run
folders>` merges runs: repeats dropped, `cap` per bucket chosen by a hash of the record (stable for the same
runs); `vectors.py show` prints a file's header, buckets and control words. Re-recording replaces the
files and the commit says why. The vectors are the original's behaviour, so a change of ours never is a
reason; an intended difference (a Rebirth fix behind its flag) needs its own check, not new vectors.

**Replay** (`ei_vectors.exe`, built in every configuration, run by build.cmd after the checks). It links
the families' own sources (no hook DLL, no game: tests/vectors/hook_stub.cpp makes EI_REPLACE register
nothing; src/lib's forwarders link but must never be called) and the checks in
tests/vectors/replay_<family>.cpp, registered under the recorded function name. Each record runs under
its recorded control word (exceptions masked), compared bitwise as the verify wrappers compare. It fails
on a differing record, a file without a check or with another payload size, or no files at all.

Families so far:
- `maths` (maths.cpp, core_maths.cpp): Vector3::Vector3, Dot, Vector3::Length, Quaternion::FromAxisAngle,
  AddVectors, NegateVector, ScaleVector, Vector3::AddInPlace, ClampUnit, Quaternion::SetIdentity,
  Quaternion::PreMultiply, Quaternion::AxisX/Y/Z, CubicSegment::SetBSpline, CubicSegment::Evaluate; up to
  256 per function. Whether the output pointer was one of the inputs is recorded and reproduced.
- `random` (random.cpp): IsaacRandom::Next (12 ordinary draws and 12 refilling ones) and Refill (8),
  the whole generator before and after.

Recorded from units, sandbox_actions, camera, combat, movement, spells_cast and unit_ai (vanilla, 2026-10-15):
16 files, 2,868 records, 271 KB; every one replays the same. Control word 007F (24 bits) everywhere but
one FromAxisAngle call at 027F (53 bits, another thread). Few distinct calls exist for ClampUnit (13),
AxisZ (6) and SetIdentity (1); none of the seven scenarios reaches CubicSegment::SetBSpline or Evaluate
(their checks are registered and wait for a scenario that does: SV5's coverage list). A corrupted copy
(one bit of a Dot result, one of Refill's state) fails both records.

**SV4a (2026-10-15): matrix maths, Range and the stat and damage formulas.**
- `maths` also has MatrixMultiply (matrix_math.cpp), Quaternion::ToMatrix (before and after of all 16
  elements, so the ones it leaves alone are checked) and MultiplyByConjugate (quaternion.cpp), the last
  with the aliasing of its output.
- `random` also has IsaacRandom::Range. The payload is the draw (results_[count_] after the call, which is
  Next's result), min, max and the float result; the replay gives ours a generator that holds just that
  draw, so the 4 KB generator is not recorded (Next and Refill cover it). Range moved from effects.cpp to
  random.cpp, since effects.cpp does not link without the game.
- `stats` (unit_stats.cpp, linked with src/lib/rebirth.cpp; Rebirth's patches stay off in the replay, the
  vectors are vanilla): UnitStats::Period20, AttributeBase, AttributeWithBonus, SkillBase, WeaponSkill,
  Reduce, ReduceKind1, LockKey::Opens, ResetDamageInfo, HasElementalDamage, ExcessDamage and MaxDamage. A
  UnitStats is not copied whole: the payload has the fields the function reads (stat10/18/20, attributes,
  skills, the six slots' kind, current, maximum and factor), the rest zero, so equal calls dedupe.
  ExcessDamage and MaxDamage moved from unit_stat_small.cpp to unit_stats.cpp (unit_stat_small.cpp needs
  net_delta.cpp, which needs the streams). Results the original leaves unrounded in ST0 are recorded as
  doubles and compared bitwise.
- Left out because their sources do not link without the game or read game memory: CTransformPipeline::
  PushMatrix (the pipeline object), UnitStats::GetFactors, StrengthRatio/DexterityRatio, SkillWithBonus,
  SkillLevelValue, Value6E8/6E9, SkillPrice and SchoolPrice (the game's settings at fixed addresses or the
  skill databases), UnitParams::ProtectionBlock and WeaponSkillBonus (databases and settings),
  UnitStats::PickLocation (the global generator). Each needs its globals passed in the payload and a
  forwarder-free way to read them: done in SV4d (below).
- Recorded from 48 scenarios (the SV4 seven and items, hero screens, camp, spells,
  scripts, models, particles, ...; vanilla): the new files are 145 KB. Reached: everything but
  MultiplyByConjugate (called from the skeleton frames), ReduceKind1 (the AI's
  target choice) and LockKey::Opens (lever and lock keys); none of the 48 reaches them; their checks are
  registered and wait for scenarios (SV5's coverage list). Few distinct calls exist for AttributeBase (1),
  AttributeWithBonus (1), WeaponSkill (1), Period20 (2), HasElementalDamage (1), ExcessDamage (1) and
  ResetDamageInfo (1). The matrices replay bit for bit at 007F, so MatrixMultiply and ToMatrix need no
  tolerance. Corrupted copies (one bit in a Reduce result, a matrix element, a Range result, a MaxDamage
  result) fail.

**SV4d (2026-10-15): the game's data seam; the formulas that read settings and the skill database.**
- The seam (src/game/game_data.h): `ei::GameData<T>(address)` is the address itself in the game; with
  `EI_VECTORS_REPLAY` (ei_vectors only) it leads to a zeroed stand-in image of 00730000-007D0000
  (tests/vectors/replay_game_data.cpp) that a check fills and replay_main clears before every record.
  The named accessors go through it: GameLogicSettings() (the registry settings, 007C53F8),
  GameDifficulty(), unitdb::Skills() / SkillLevels(), SkillDb(), IsaacRandom::Global(). The formulas
  now read those by name (hpVal[0], mpVal[0], skillVal[0..1], perkPowerBase, backstabAdd, absorption)
  instead of raw addresses; GetFactors' 007AF280 and ProtectionBlock's slot weights (0073E0FC) read
  through GameData. A record's payload holds what the call read there (settings, the level record's
  value and worth exponent, a skill's first stat letter, the weapon's skill bonus bytes, the
  generator's draw); the replay writes it to the stand-in and builds raw-byte database records
  (never constructed: a CString constructor reads the game).
- Moved so that ei_vectors links them: StrengthRatio / DexterityRatio to unit_stats.cpp, SchoolPrice and
  SkillDatabase::IndexOfLevel (0051FF00, which SkillPrice now calls directly instead of the original)
  to unit_skill_price.cpp. ei_vectors also links unit_hit_location, unit_params_protection and
  unit_params_skill.
- `stats` gains SkillWithBonus, SkillLevelValue, Value6E8, Value6E9, StrengthRatio, DexterityRatio,
  GetFactors, SchoolPrice, SkillPrice, SkillDatabase::IndexOfLevel, UnitParams::ProtectionBlock and
  WeaponSkillBonus, UnitStats::PickLocation; `maths` gains CTransformPipeline::PushMatrix (a raw-byte
  pipeline, stack 0; whether m was the slot written is recorded). Calls whose reads leave the recorded
  shapes (an index past the skill bytes, a slot type past 8, a difficulty past 2, a pick that refills
  the generator) are not offered.
- Recorded from 38 vanilla scenarios (units, combat, items, HUD, hero, trade and camp screens, spells,
  camera, models, menus, ...): 12 new files, 57 KB, every record replays bit for bit at 007F. Not
  reached: Value6E9 (backstab), GetFactors (hero creation), PickLocation's random draw; few distinct
  calls for ProtectionBlock (1), SkillLevelValue (1), Value6E8 (1), the two ratios (1 each),
  SkillWithBonus (2), WeaponSkillBonus (3). Corrupted copies (one bit in a SkillPrice, SchoolPrice,
  WeaponSkillBonus, ProtectionBlock and PushMatrix result) fail.

**SV4b (2026-10-15): variable-size records; the LZ coder and the script lexer.**
- Format: payload size 0 in the file header means each record is its header, a u32 size and that many
  bytes, written with `Writer` and read with `Reader` (vectors.h; `Sampler::OfferBytes`,
  `EI_VECTOR_CHECK_BYTES`). A check must read its payload to the end. Large state is compared by
  `HashBytes`. vectors.py reads, merges and shows both kinds.
- Fixtures: a function name starting with '@' is a fixture. Its files replay first and the game data
  it writes stays for the other checks (KeepGameData; ResetGameData restores it). After a failed
  fixture ei_vectors stops. The stand-in now covers 00401000-007D7000, which includes code immediates
  such as ClassifyToken's keyword table operand at 004CF69B.
- Calls from our own code: callers of CoderState::Compress / Decompress are rewritten, so the original
  is never called at its address. `EI_RECORD_OURS` (hook.h) also patches our replacement's body (after
  the incremental-link jump stubs) with a jump to the recorder in record mode. It refuses when another
  address shares that body.
- `crt`: the game sets a locale (cp1251). The fixture "@GameCrtCtype" holds isdigit, isspace and tolower
  for 0..255; ei_vectors' gamecrt answers from it (tests/vectors/replay_crt.cpp, with operator new,
  malloc, strncpy, strncmp and strnicmp; src/lib/crt.cpp leaves those forwarders out under
  EI_VECTORS_REPLAY). **Finding**: isdigit / isspace of a negative char (the lexer passes signed chars
  >= 0x80) read heap memory before the locale's table, so in the game the answers differ from run to
  run. Ours calls the same CRT and does the same. A record whose input has such bytes therefore carries
  the answers given at the call (crt::NegativeClasses).
- MFC in the replay: tests/vectors/replay_mfc.cpp is MFC 4.2's CString (refcounted data, the nil string)
  and CPlex on the replay heap. src/lib/mfc.cpp leaves those forwarders out under EI_VECTORS_REPLAY.
  registry.cpp's ConstructString now uses the new `CString(const char*, int)`.
- `coder` (net_coder.cpp, game_buffer.cpp): the fixture "@CoderTables" is InitCodeTables' output,
  compared from index 1 (the game's index 0 is the static data's), plus the bit masks at 0078DB24.
  The tables and masks now read through GameData. CoderState::Compress and Decompress (8 per size
  bucket: up to 64 B, 1 KB, 16 KB) record the input, the written bytes, the scalars, the 10 KB match
  tables (before; after as a hash) and the buffers' cursors. The replay builds a GameBuffer subclass
  that seeks like GrowingBuffer or BufferView, as recorded.
- `script` (script_parse.cpp): the fixture "@ScriptKeywords" holds the keyword table, quote, comment
  markers and the "None" / "Unknown command" texts. The replay builds the table in the stand-in. Then
  ClassifyToken (128 per bucket: number, string, none, keyword), FunctionSignature, KeywordName,
  CScript::Tokenize (whole source; token count, list hash and per-token hashes, or the error code,
  line and text) and ApplyDefines. In the replay, ClassifyAt / SignatureAt / KeywordNameAt call ours
  directly; in the game they stay naked forwarders.
- Recorded from 16 vanilla scenarios (script_*, world_load, saves, units, combat, lobby, menus, items,
  ...): 10 new files, 520 KB (Compress 283 KB), all replay the same at 007F. KeywordName has one
  distinct call. Tokenize has 8 sources up to 55 KB and no 1-16 KB one. Corrupted copies (one bit in a
  Compress / Decompress output, a ClassifyToken id, a Tokenize hash, an ApplyDefines line, a
  FunctionSignature, the coder tables, a keyword record's length, isspace('a')) fail.
- Not covered: CScript::Load / LoadFunction / LoadCommands (they need a CScript and its CArrays built
  outside the game), ExpectLexeme (SV4b1).

Adding a family: a payload header, a record file (one recorder per function, `Sampler(family, name,
sizeof(payload), cap)`), a replay file, the family's sources in CMakeLists.txt's ei_vectors, then a record
run and `vectors.py merge`. A family whose functions read game objects needs those objects' state in the
payload (copied, never pointers) and, for the replay, a way to build them outside the game.

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

## SV1b: the heap, the update countdown and the nearby list (2026-10-15)

combat, combat_more, sound and rebirth_flags did not repeat on the virtual clock, on either side.

**The heap.** game.exe's CRT (VC6, statically linked) puts every malloc on a Win32 heap of its own
(_crtheap 007CAF08, `HeapCreate(0, 0x1000, 0)` in _heap_init 006F54E6; on NT 5+ __heap_select 006F539E
picks the system heap, so there is no small-block heap in front). Windows 8+ gives such a heap the
low-fragmentation front end, which picks a bucket's free slot at random: the same calls get other
addresses in every run. tests/verify/harness_heap.cpp, on the virtual clock and on both sides: the
harness (static constructor, before the CRT starts; ei_launch starts the game suspended) patches game.exe's
HeapCreate import so the CRT's heap is made with HEAP_NO_SERIALIZE (no front end), and HeapAlloc /
HeapFree / HeapReAlloc / HeapSize / HeapDestroy take a critical section of the harness's for that heap.
With it, three runs of the original are the same in every digest field of every record (combat 630,
combat_more 635, sound 530 records, `--all-fields`); with `--clock virtual,lfheap` (the game's heap)
combat's three consoles differ. Other threads that allocate from the CRT heap could still move
addresses; none showed up.

**The update countdown.** Each side now repeats, but ours and the original allocate differently (the
verify wrappers run originals on scratch copies), so units sit at other addresses on the two sides,
and the countdown (0x80 + bits 5..8 of the address) differs. The harness makes both sides spread it by
the object's id (`id_ & 0xF`) instead: the eight original sites (`SAR r,5 / AND r,0xF` -> `MOV
r,[r+0xC] / AND r,0xF`; 0058D4FA WakeUnitsAround, 0058D5E2 AddToUpdateMap, 0058D8F7 UpdateUnits,
00592B14 / 00592C81 / 00593131 CreateObject, 005940DD InvokeAlarm, 00596BB1 the save's update map) and
CWorldServer::UpdateCountdownFor in ours (world_server.cpp; AddToUpdateMap, UpdateUnits and
CreateObject call it). Outside the harness it stays the address. golden.py no longer leaves the
field out.

**AIViewer::Refresh (005CA1D0), a rewrite bug.** With both, ours and the original still printed two
"Unit A sees unit B" lines of one update in the other order (combat, combat_more, sound), with every
digest field the same. The original inserts each unit near the viewer at the head of the brain's
nearby list (an inlined AddHead); ours appended them (AddCountedTail), and See walks the list from the
head. Fixed (unit_ai_sight.cpp). The wrapper (verify_unit_ai_sight.cpp) is ours-only and checks the
list's members, not their order.

**rebirth_flags** repeats on the fixed heap (the group order was by address). Its last difference
was its own: `#DistancePointPoint(3,4)` printed the two floats addon's handler reads past its
arguments (5, 1.#INF, or a NaN that made the variable's verify fail when stored); the scenario now
checks `Not(IsLess(DistancePointPoint(3,4),0))`, true for any of them.

Results (on top of SV1a): the four scenarios, three runs per side, repeat; combat, combat_more and
sound print the same as the original. Left: in sound the digests part at update 272 in `crt.rand` only
(StartIdleAnimation -> FindAnimation one frame earlier in ours, a client-side pick) without a
difference in the console; obj2 (unmoco2)'s hit points are an unwritten field in ours (NaN / 2e14
against NaN / 1.0 in the original's first record).

What the repeatable heap changed elsewhere in the suite: the CRT heap can now begin right above
game.exe's image (0x007F...), and netverify::BuiltCompare took everything below 0x800000 for an image
address, so CBaseManager::ConstructBase's bucket pointers failed to pair (six scenarios); InImage (and
ui_census's vtable range) now end at the image's real end, 0x007D7000. ui_hud under Rebirth: zone1's
lone wolf (obj1804) reached the hero during the HUD walk and the "hero died" box covered the HUD on
every run; the scenario now has the hero in god mode until its last check.

## SV1i: no more `speed 10` (2026-10-15)

On the virtual clock `speed 10` gave each frame 50 ms of virtual time instead of 275 (five updates a
frame at any length), so anything that waits on sound ends or the clock took 5.5 times the frames,
and every `wait <ms>` ran 5.5 times the world updates at about 100 ms of real time per frame.

Two options were tried, on the same build and machine (4 games at once, vanilla):

- (a) `speed` with a virtual-clock meaning: a frame always advances 275 ms, and the main loop's pass
  limit (the original's `LEA EAX,[EAX+EAX*4]` at 0045725C, five updates) patched to the frame's
  length, so `speed 10` runs 27-28 updates a frame. Same world updates per `wait` as before, but a
  frame with 27 updates costs far more than one with five (the verify wrappers run in every update):
  movement 44.7 -> 23.3 s, rebirth_flags 60.4 -> 28.8 s, against 15-18 s for (b). And 27 updates
  between two interface / input / sound-end steps is a different game: unit_ai_fear's fight no longer
  happened (161 functions fewer) and effects_bow's clickunit missed its wolf. Not taken.
- (b) `speed 10` dropped from all 77 scenarios (taken). A `wait 1000` is now 18 world updates instead
  of 100, so the world does 5.5 times less in each fixed wait. The verify call counts of each
  scenario before and after (work.db's run.counts) show what that cost: the suite as a whole lost 7
  functions, all where a scenario's purpose needs world time, and those scenarios now wait for it:
  hero_death (the death and game-over messages: wait 10 s), unit_ai_fear (the fight: 16 s),
  spells / spells_cast (the wall and fog ending: AI map footprints and occluders removed: 11 s),
  spells_hero (FogDestroy: 16.5 s), rebirth_flags (the snow fades in over 40 updates: 4 s),
  effects_bow (the shots and the loot: 10 s and 5.5 s), sandbox_actions and sound (the death's own
  updates after the kill: 3 s more), ground_prints (the intro blocks the hero a few updates in:
  `waitfor IsUnitBlocked` before unblocking, and the walks wait for the hero's position), script_exec
  (a script `Sleep(200)` is 200 updates, 11 s: waitfor 20). What stays lost is incidental and reached
  by other scenarios: zone1's scripted storm (lightning, rain, timed objects) and environment remarks
  that came with the longer world time, idle animations, armour model queries.

Times (vanilla, the 77 scenarios, 4 at once): 1616 s before (batch 209), 1053 s with `speed` dropped
alone (batch 218). The whole suite (117 runs, both installs, 4 at once, batch 280): 1586 s against
1992 s for the last comparable suite (batch 211, 115 runs, 16 at once) and 2584 s for SV1f's (batch
200). Biggest wins: movement 42.7 -> 15.9 s, rebirth_flags 38.4 -> 19.9 (vanilla) and 42.7 -> 24.8
(rebirth), ui_widescreen_more 38.1 -> 17.5 (rebirth), ui_widescreen_hud 34.0 -> 17.5 (rebirth),
map_objects 34.0 -> 17.6 (rebirth), particles 24.4 -> 10.7, world_client 28.4 -> 16.5.

Every one of the 77 scenarios changes its digests (the waits cover other world updates, the camera
moves by 55 ms ticks): the goldens of camera, console, hero_death, items, levers, objects_more,
party_records, render_movie, render_movie_present, render_scene, render_terrain, sandbox_actions,
script_copy, script_parse, script_quest, spells_cast, ui_battle_spells, ui_container_input, ui_help,
ui_objectives, ui_order_move, ui_quest_notice, ui_saveload, ui_saveload_delete, ui_screens,
ui_trade_restore, unit_create and world_load need recording again (SV3).

## SV5: the coverage list (2026-10-15)

**How a function counts as reached.** `EI_COVER=hooks` (src/hook/cover.cpp) puts a one-shot INT3 on
every installed hook's address and on the first byte of its plain replacement (our function), so calls
from ours to ours count as well as the game's calls through the JMP; the first hit logs `cover ADDR`
in ei_hook.log. Limits: a call the compiler inlined is not seen; a replacement shared by two addresses
marks both; static initialisers run before the hooks exist (they have EI_STARTUP_CHECKs instead).
`EI_COVER=<addr,...>` (the old mode) still watches single addresses.

**The tool.** `tools/coverage.py`:
- `collect <run folders> [--add]` reads the `*-vanilla` runs whose log says `address(es) from hooks`
  and writes `tests/coverage/reach.txt` (per hook, a bitset of the scenarios that reach it; `--add`
  keeps the scenarios already there). Collected with `EI_COVER=hooks python tools/work.py test <ID>
  --vanilla-only` over the whole suite.
- `list` writes `tests/coverage/coverage.csv`: per rewritten function (EI_REPLACE names and files,
  sizes from function_sizes.inc, the wrapper kind from EI_VERIFY's stolen bytes) its status: golden (a
  vanilla golden scenario reaches it), vector (tests/vectors/data has its file; the recorders map
  addresses to .vec names, following one helper level), suite (only scenarios without a golden),
  unreached, startup / dead (`tests/coverage/unreachable.txt`), not-installed (vanilla). Risk, for
  suite and unreached: size x area weight (game state 3, network / UI / libraries 2, drawing and sound
  1) x2 when unreached x1.5 when the wrapper is ours-only (it checks less than a comparing one).
- `who <address or name>` (which scenarios reach it), `top [N] [--area]` (the ranked gaps).

**Numbers** (97 scenarios): 6059 hooks installed in the vanilla run, 4668 reached. Of 6220 rewritten
functions: 3737 golden, 7 vector, 924 suite, 1384 unreached, 6 startup, 1 dead, 161 not installed.
By risk the largest areas are multiplayer-only (net_messages.cpp alone has 117 functions; SV5d), the
fake unit scene loader (SV5c), camp crafting and exchange screens, item info descriptions, and AI
modes that only zone units take (SV5a).

**The known gaps.**
- CubicSegment::Evaluate 0040E440 and SetBSpline 0040E3D0 are dead: the only caller of 0040E440
  (0040DEB0) has no references, and nothing calls SetBSpline. The game uses a byte-identical copy of
  Evaluate at 004E5660 (camera and AI curves); record_maths.cpp records it into the same sampler and
  `CubicSegment__Evaluate.vec` (256 records) now has data. SetBSpline's check stays without data.
- UnitStats::ReduceKind1: recorded from the new unit_ai_heal (one distinct call).
- MultiplyByConjugate: only MakeFrameRelative calls it, inside the fake unit (class 0x39, .amf) scene
  loader, which no scenario reaches (SV5c).
- LockKey::Opens: only Lever::KeyOpens and Lever::ApplyKey call it, a quest item used on a locked
  lever from the quick item panel (SV5b).

**New scenarios** (all pass on vanilla with hooks; none has `speed`):
- unit_ai_heal: zone1's wolf 1804 gets a healing spell and an alarm rebuilds its options; the AI's
  Score -> ReduceKind1 path.
- ui_base_topic: a base topic (b.Smith.M4) in bz1g; StartDialogue, StartPhrases, NextPhrase,
  ShowItem, FinishDialogue, CompleteBriefing (0 mismatches).
- world_loot: three dead wolves, `lootall` (LootDeadUnits, Loot) and the camp's `give 0 assortment`
  (CCampPages::DebugFill).
- unit_ai_cast: the wolf casts a fireball (CSpellEffect::UpdateMissile); OrderCastAt is still not
  reached.
- zone_gz15h_tornado: a tornado zone; CEffectTornado's Launch, Tick and Reset (69 s on the clock).

**Goldens not recorded** for them yet, all for known SV1 classes: unit_ai_heal (ours parts at update
640: isaac.count, an AI unit's standCooldown), unit_ai_cast (update 720: obj699 ai.looks 3 against 2,
and the original's "acknowledges with idleSound" line missing in ours), ui_base_topic (the original
does not repeat at update 80 in bz1g) and world_loot (the original does not repeat at update 200,
isaac.count, with the same console). zone_gz15h_tornado was not tried (69 s per run).


## SV3d: the goldens recorded again (2026-10-15)

After SV1b (repeatable heap), SV1i (no `speed 10`), SV2a (digest 2) and SV3a (unmoco2's hit points), SV3's recipe over
the whole suite (orig twice with `--fresh`, ours twice, EI_DIGEST=20, 4 games at once): **56 goldens** in digest 2,
vanilla 46: camera, combat, combat_more, console, ground_prints, harness_digest, hero_death, items, levers, magic_traps,
map_objects, movement, net_lobby, objects_more, party_records, render_movie, render_movie_present, render_scene,
render_terrain, render_titles_present, sandbox_actions, sandbox_map, script_copy, script_parse, script_quest, smoke,
spells, spells_cast, terrain_water_load, ui_battle_commands, ui_battle_spells, ui_help, ui_hero_delete, ui_objectives,
ui_options_bind, ui_order_move, ui_quest_notice, ui_saveload, ui_saveload_delete, ui_screens, ui_trade_restore,
unit_ai, unit_ai_fear, units, world, world_load; Rebirth 10: console, ground_prints, harness_digest, items, levers,
map_objects, script_parse, ui_help, ui_widescreen_hud, ui_widescreen_more. ui_main_menu, ui_hero_screen and
ui_hero_stats keep their digest-1 goldens (the four runs match them; digest 2 parts in cunit2's rotation, SV3f).
ui_container_input's and unit_create's old goldens no longer hold (SV1i; not re-recorded) and are to be dropped.
cunit2's vitals now agree with the original everywhere, so KNOWN keeps only SV2b's entry. A fifth suite with `--golden`
(ours, 117 runs): 59 the same, the two stale ones differ.

Repeats: the original 87 of 106 passing runs, ours 98 of 114. Not recorded (by the first difference; `@N` world update):
- *SV1j family* (isaac.count first, with zone1's idle wolf obj1936 `ai.standCooldown` and/or the hero's client
  `animationStart` a few ms apart; both sides flip between the same alternatives, so a harness timing leak, not a
  rewrite): effects, effects_bow (both), frames_basic (rebirth), particles, rebirth_flags (rebirth), spells_hero,
  trader_stock (both), ui_camp (both), ui_camp_deals (both), ui_describe, ui_global_map_pick, ui_help_page (rebirth),
  ui_quick_items (both), ui_trade_hold (both), ui_trade_restore (rebirth), ui_view_screen, ui_widescreen_screens
  (rebirth), script_fx, zone_base_bz1g, ui_mp_host (both); crt.rand only in the original's own runs: ui_base_dialogue,
  unit_create (SV2c), zone_gz3g.
- *Ours against the original, real:* stored script runs keep the instance as kill flag (SV3e: frames_basic,
  render_cpuwindows, render_present, ui_base_dialogue, ui_help_page, ui_hud both, world_client, zone_base_bz1g,
  ui_mp_host); client unit rotation by an ulp (SV3f: ui_main_menu, ui_hero_stats, ui_hero_screen, ui_container_input,
  ui_widescreen*, script_exec); cunit129 flag 0x400 (SV3g: ui_widescreen, _hud, _more, _screens vanilla); obj2 hp NaN
  payload (SV3h: render_present_cpuwindows); zone_gz3g's VSS#i#val between our runs (SV3i, SV2b on vanilla); crt.rand
  (SV3j: sound, zone_gz2g_traps, models).
- *Fail without our hooks:* rebirth_flags (vanilla), render_cursor_present (both: no cursor-thread presents without our
  renderer), render_device_lost, render_present_shot (both), render_present_window; render_present_fullscreen is not run.

## T2: remaining fixed waits and the weak scenarios (2026-10-15)

T1's follow-ups. Coverage was compared with run.counts in work.db (batch 280/303 against the final
suite, batch 438): no scenario lost a function that is not on the flaky list (TellParties, ManaCurve,
OnUnitLeft, SoundStreamRead, speech and attack-param groups).

- run_all keeps the first attempt's log on a retry: `keep_attempt` copies ei_hook.log and
  harness_report.txt to `runs/<folder>-attemptN` before the next try, and the PASS* note says where
  (work_sched passes the batch's build). Test: `Retries.test_failed_attempt_is_kept`.
- combat_more reaches `CUnitServer::EndOrder`: four wolves of player 6 placed 14 tiles away with
  the hero on GodMode, `Attack` by the hero, then `SetCPFast` moves the targets out of reach (the
  target is out of reach in UpdateState).
- ui_hero_delete reaches DeleteHero: the defaults button (region 4) clears the name field, so the
  name is typed after it, then regions 1, 1, 7 and `waitwindow 10 msgbox`, enter. Golden recorded again.
- Fixed waits became waits on state or shorter waits in sandbox_map, hero_death, unit_ai_fear,
  spells_hero, spells_cast, movement, effects_bow, particles, objects_more, sandbox_actions, sound,
  zone_gz3g, zone_gz2g_traps, effects, map_objects, units, ui_mp_host, levers, ui_hud, ui_trade_hold,
  ui_global_map_pick, ui_saveload_delete. The four ui_widescreen scenarios: `wait 6000` is now
  `waitmenu` + `wait 5200` (the menu is up after 825 ms; 5200 keeps the frame count, so the menu's
  late draws and the world's random draws stay as they were).
- Lesson: any change in frame count reshuffles the ISAAC draws. Functions driven by them (the
  zone1 storm, wolf pathing, the bored remark) are lost when a wait is cut, even if the wait only
  looks idle. Cuts that lost a function were taken back (spells, models, unit_create,
  terrain_water_load, unit_ai, ground_prints, rebirth_flags, ui_base_dialogue, world_client).
- `PickEnvironmentSpeech` stays unreached reliably: only the server's bored remark (boredTicks > 500,
  then 1 in 1001 per update) calls it. T2c.
- Numbers: frames (GameFlow::UpdateMultiplayerVariable calls) 115765 -> 112876 over the 117 runs
  (-2.5%), because most of a run is its start. Wall seconds are too noisy under load to compare
  (batch 280 1586 s with 4 at once, batch 438 2050 s while other batches ran; the suite itself took
  148 s of scheduler time on idle slots).
- Open: ui_base_dialogue (Smith has no topics, T2a), a hero attack on a removed target gives a
  PlayerMode::UpdateCombat mismatch (T2b), goldens to record again (T2d), rebirth_flags'
  `DistancePointPoint(3,4)` check prints 0 on a first run now and then (T2e, unchanged scenario).

## SV3c: Rebirth with the flags off (2026-10-15)

With `FollowAddon=0` every Rebirth golden parted at world update 40 (three ISAAC draws more). The random traces of the
original and ours (flags off) parted in the new game's load: ours filled fewer terrain tiles (terrain FillTiles draws)
before SaveParty drew. Cause: `GameRender` (00578910) called our `CWorldClient::Render` directly, so with the flag off
our Render drew the terrain at the vanilla radius 100 while addon's patched 00569E40 (LandscapeDrawRadius, the starter
setting) was what the original runs; the terrain view loads the sectors within the radius. Rule 9 again: a rewrite
calling a Rebirth-patched function must go through its address.

A scan of ei_hook.dll for direct calls to rewrites of the functions in docs/rebirth-patched-functions.txt (capstone over
.text, incremental-link thunks followed, callers named from ei_hook.map) found three more, all now through their
addresses: `CHudPortraits::OnMouseUp` -> 00638EF0 SelectUnit (FixSpellSelfCasting; `SelectUnitAt` moved to
originals_ui.h), `CWorldServer::RemoveAllUnits` -> 005935C0 RemoveObject (FixRemoveMapObject), `GameSetUnitHeight` ->
005C11D0 CSectorMap::Move (FixMoveObject). The other hits are verify wrappers, a function calling itself, the script
parser / evaluator family (one flag, skipped or hooked together), and two calls the original inlines: ProcessRequest's
StartNewGame (0057BD10's steps inlined in 0057C700) and CMMIOResource::Destroy's base destructor. Inlined calls are not
seen by such a scan.

Checked: flags off, the ten Rebirth goldens the same (console's random trace makes every draw of the original's in the
same frame); flags on and vanilla, 19 goldens of 12 scenarios the same.


## SV3e: stored script runs clear their kill flag (2026-10-15)

004BC420 sets its local run's kill flag to 0 after Execute returns unfinished, before the copy into the run list
(`local_b0 = 0`); ours kept the instance (script_exec_run.cpp now clears it; the verify wrapper checks for null, ours only).
The script group of digest 2 no longer differs in the nine scenarios: ours match the original wherever the original
repeats. Recorded (SV3's recipe, 9 scenarios x 2 installs): ui_hud vanilla, world_client vanilla and rebirth.
The others stay out for other causes: the SV1j family (isaac.count/standCooldown; crt.rand in the original's own runs)
for frames_basic, render_cpuwindows, render_present, ui_base_dialogue, ui_help_page, ui_mp_host, zone_base_bz1g rebirth;
zone_base_bz1g vanilla also shows SV3f's rotation ulp; ui_hud rebirth differs from the original from world update 40 in
the script global gobject.i (702 in the original, 1884 in ours; both repeat), SV3k.

## SV2b / SV3k: the harness heap in one reservation (2026-10-15)

**Cause.** zone1's `i` is the loop variable of Rebirth's `Any(i, Peasans, ...)` / `Every(i, Peasans, ...)` over
Peasans = {702, 1884, 699} (zone1.mob's script chunk, decoded as 004D8E10 does), a group kept in address order (004B2D80).
The heap trace (below) showed the units in different segments of the CRT heap: 1884 (allocated first) in one, 699 and
702 in a later one, and the later segment's place is wherever VirtualAlloc found room, which our renderer's and the
driver's threads change between runs (0x2A45.. vs 0x2CF1../0x2E5F..; the original 0x20E1.. vs 0x2750.. every run).
So which unit was last flipped between our runs. Further sources found by tracing zone_gz3g (SV3i): blocks other threads
allocate (the sound thread's stream switch reads the registry through CFileRegistry; the CRT makes lock 0xB and runs
__tzset, 006F8AA1, on the first file time any thread converts), and MFC's temporary handle objects
(CHandleMap::FromHandle 0070367C from CDC::FromHandle(GetDC(..)) in 005F8340: whether the DC is in the temporary map
already depends on the handle values Windows gives, and the idle cleanup frees them in hash order).

**Fix** (tests/verify/harness_heap.cpp, both sides): the CRT heap is one fixed 256 MB reservation (HeapCreate with a
maximum, so it never grows a segment) for the main thread's blocks below 0x7F000; larger blocks, other threads' blocks
and blocks made inside CHandleMap::FromHandle come from a second, growable heap (frees and reallocs go by address). At
the first message loop pass the harness takes every CRT lock once and runs __tzset on the main thread (SettleCrt).

**Tool:** `EI_HARNESS_CLOCK=virtual,heaptrace` writes heaptrace.txt (every call on the CRT heap: op, size, blocks as
reservation offsets, ten return addresses from a stack scan; `*` for other threads) until world update `EI_HEAPTRACE`
(default 40), then every object's id, class and place. Two runs' traces are compared event by event (reservation
events of the main thread only); callers resolve with ei_hook.map / config/functions.csv (tools/resolve_crash.py).

**Effect.** The original's layout changed too, so every golden was recorded again (SV3's recipe, the 55 golden scenarios
and zone_gz3g, both installs): the original repeated in 111 of 112 runs, ours in 109; **97 goldens** (vanilla 44, Rebirth
53, was 64), ui_hud Rebirth among them. The six Rebirth scenarios of SV2b gave 702 in 12 of 12 runs. KNOWN: `script
gobject.i` dropped; `obj2 hp` added (SV3h): unmoco2's hit points are the NaN its stats' stack leftovers give, which on
this heap differ by scenario and install in the original (0xFFFFFDxx vanilla, 0xFFFFFFxx Rebirth) and repeat; ours keeps
SV3a's 0xFFC00000. Not recorded: SV3f's rotation ulps (ui_main_menu both, ui_hero_screen, ui_hero_stats, ui_screens,
ui_container_input, ui_widescreen_hud / _more vanilla) and SV3g's flag; gobject.i after a save is loaded, 702 in the
original and 1884 in ours, both repeating (world_load, magic_traps vanilla; SV2d); script_copy vanilla, where ours'
CPlayerServer::Flush (in LeaveVerify) compressed its stream to 1392 bytes in one run and 1408 in another, so every later
block moved (SV2e); unit_create both (crt.rand between our runs, SV2c); zone_gz3g (SV3i: the original's VSS#i#val now
differs between its runs in vanilla, ours from the original under Rebirth).

## SV6: no registration skipped unnoticed at hook install (2026-10-15)

The hook build left 224 registrations out at install (smoke, vanilla), so those rewrites never ran there:
- 83 starts with no Ghidra function (spell effect Emit/Update/Tick slots 004889D0..00496840, reached through
  tables; CPlayerServer 0066D7B0, 0066EAC0, 0066F1F0): sizes decoded with capstone (recursive descent within the gap,
  two switch tables followed) and added to config/extra_function_sizes.csv and function_sizes.inc.
- 64 functions under five bytes registered with EI_REPLACE / EI_VERIFY (RET, RET n, XOR EAX,EAX; RET, ...): game.exe
  now gets their jump too when the bytes after them up to the fifth are NOP/INT3 padding on disk and in memory before
  the next known function (InstallHooks, PaddingAfter). EI_REPLACE_SHORT ones stay the original's in the hook build.
- 59 second registrations of one address: the EI_*_VERIFY macros of verify_player_messages.cpp and
  verify_world_effect.cpp registered besides the EI_VERIFY line after each use (the macros now only define;
  0066D490 had no line and got one), and 00669D50 had both an observer (verify_world_client.cpp, from before
  OnGameState was ported) and OnGameStateVerify: the observer's counters moved into OnGameStateVerify
  (game_state_message.h).
- 9 the harness's own patches (the eight SV1b countdown sites in seven functions, the fixed ISAAC seed at 0049F660,
  GameMain's redirected CALLs): expected.

InstallHooks now classifies every SKIP. Expected: the harness patched the function (`ei::NoteHarnessPatch`, called by
the harness's RedirectCall / JumpTo / JumpFrom / capture CALL / countdown sites; "the harness patches +0xN (...)"),
a Rebirth site the rewrite does not implement, and, in a Rebirth launch only, a byte that differs outside every known
site. Anything else (unknown start, short without padding, a second registration, thunk, dispatch, trampoline) is
logged with " (unexpected)", counted, summed up in the log, and the harness report gets a FAILED line, so the run fails.

## SV6a: zone_gz15h_tornado ran 1.6 times slower on Rebirth (2026-10-15)

Same 2805 world ticks and 563 frames on both installs, but Rebirth lights 2.5 times as many terrain sectors
(SectorData::Light 44198 calls against 17734; its map has more moving point lights), and every light call runs
through the nested LightVertex / ShadeSun / AddDirectional verify wrappers: 88 million Check calls in the Rebirth run.
EI_PROFILE=1 put about half of the main thread in ei::verify::Report, not in the lighting: it built a std::string
and walked a std::map per check (and Check zeroed a 256-byte buffer). Report now finds its entry through a table keyed
by the name literal's address (confirmed by comparing the text), Check/CheckNear leave the detail buffer unset.
The wait of 150000 went from 88 s to 45-49 s on Rebirth (whole run 105 s -> 62-68 s, vanilla 65 s -> 42 s, 2 at once,
profiled); the 120 s limit and the 150 s wait stay.

The scenario reaches CEffectTornado (Launch, Tick, Reset) on Rebirth only, and not in every run: a strike needs open
ground at a random point (about 3 % of the 64-tick weather updates, 44 in the wait), so runs with 72 to 90 s waited
launched one in about half the cases and the 150 s wait is what makes it likely. Vanilla did not launch one in any run.
Shortening the wait would lose the coverage; no golden is recorded for it (the strikes differ between runs).

## SV6b: rare under-load failures after SV6 (2026-10-15)

- CPlayerClient::SendResume (0066B300) returns whatever its stream Write call leaves in EAX, and 004A53F0 leaves
  the new position there (a void slot). Our MemoryStreamWriteHook and WriteVerify returned nothing, so the original
  SendResume (and the original TakeExit / RequestZone / SendLeave04 tails, and any game caller using the slot's EAX)
  returned leftovers of our code; in the verify build it was usually 1 by chance (the stream held only the resume
  byte), hence one mismatch in a suite. Both now return position_, and WriteVerify checks the original's EAX
  against it. CompareSend's message names what differs (header with size / position, bytes, results).
- sound-vanilla: the second goblin (Snd2) killed the hero in the 2 s after the first one died (its hits roll
  differently from run to run: the AI messages' order varies under load), twice in main suites. The scenario
  removes Snd2 once Snd1 is dead. Nine repeats of sound + render_cursor_present (4 at once, other batches running)
  passed.
- Full verify suite (128 runs, 4 at once) passed; zone_gz15h_tornado-vanilla passed on its retry after the first
  attempt crashed at startup inside AIL_waveOutOpen (the harness's MutedWaveOutOpen from CSoundManager::Open, main
  thread, EIP 00007EBC, no module): not seen before in the board's history; on watch (SV6c).

## SV2f: S7's verify wrappers moved the game's heap blocks (2026-10-15)

Main's batch 632 had five Rebirth zone1 new games (ground_prints, harness_digest, map_objects, script_parse,
ui_trade_restore) part from their goldens at world update 40 with the same hashes (176 more ISAAC draws; obj1804, 1890,
1936, 1937 `#ai`). In a worktree the five mostly matched, but plain full `--golden` suites had render_scene and
render_movie (vanilla) part the same way. Disabling S7's 104 addresses, then halves, then single ones, narrowed it to
the checks: disabling only 005BE7B0 (List1C::NewNode) made render_scene match. The goldens are right.

The cause was the check order, not S7's code. A twin check that grows the list (NewNode, AddTail / AddHead, NewAssoc
with the free list empty) ran the original first on the twin header, so the original's CPlex block took the address the
block gets in a `--no-hooks` run, ours took the next one and the twin's was freed, leaving a hole. InitHashTable copied
the old table into a game-heap block before either ran, and ConstructElements' twin buffer was a game-heap block too.
The game reads the heap's address order (ISAAC is shared by the client's particles and the server's AI, so one draw
moved changes the AI), and whether zone1's start went the other way then depended on timing. Now ours runs first when a
block is carved (the original second, on the twin, its block compared by position and freed), InitHashTable runs ours
first and copies the old table for the original afterwards, and ConstructElements' twin is our memory. The CArray and
plain-array checks (S5, S6) had the same order and now run ours first too, the twin made afterwards from a copy kept in
our memory. Rule for twin
checks of allocating functions: ours allocates first, the original's allocations come after and are freed.

Tool: `EI_HEAPSEQ=<update>` keeps the heap trace's events in memory (callers six deep) and writes heapseq.txt at that
update, so tracing does not slow the run down.
