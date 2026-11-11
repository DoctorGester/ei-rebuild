# Verifying changes

There is no original code to compare with at run time, so every check is recorded beforehand: whole-run goldens of our
own build, frame references for Rebirth's WideScreen, and unit checks (tests/unit) for what needs no game. This note says
how that works, what the harness does to make runs repeat and keep them apart from the machine, and the rules that came
from real differences. Running scenarios and the steps: harness.md.

## 1. How verification works

- **A change is checked** by a golden scenario that reaches it, or by a unit check where no game is needed.
- **The suite**: `python tools/work.py test <ID> --golden [run_all args]` plays install `standalone` (vanilla data,
  Rebirth's bug fixes on: the defaults) and compares every run that has a golden. Rebirth's data (`standalone-rebirth`,
  the Rebirth folder's profile) runs the Rebirth set (`--rebirth-only`: every scenario with a Rebirth golden, the
  marked and the stepped ones; harness.md), on main after every merge. GOLDENS THE SAME / GOLDENS DIFFER
  sets the exit code.
- **Goldens are our own**: two runs of the build that agree (section 2). Until the switch every golden was the original
  game.exe's and the build matched all of them; the git tag `original-oracle` marks the last commit that recorded from
  game.exe: check a question about the original's behaviour there.
- **Heap layout**: malloc is the host CRT's, whose low-fragmentation heap moves its blocks in every run, so a golden
  that held across runs holds under any layout.
- **WideScreen references** (section 2): frames of Rebirth's own WideScreen, compared on the Rebirth install.
- **Out of sight and on loopback** (section 6): harness games never take the foreground, their windows are cloaked,
  every game binds and sends on 127.0.0.1 only.

## 2. Goldens

**What a golden holds** (tests/golden/<scenario>/<install>/, tools/golden.py): golden.json (date, commit, digest period,
fields left out, how many runs agreed, the reason, `"source": "image"`), console.txt, digest.txt and dump_*.txt in
compact form, and saves.txt. A difference names the record, the object and the group; for the fields behind a hash, run
the scenario at the golden's commit again (it repeats) and `golden.py diff` the two full digests.

**The digest** (tests/verify/harness_digest.{h,cpp}; `digest <N>` step or `EI_DIGEST=<N>`, the suite uses 20,
`run_all.GOLDEN_PERIOD`): one record every N world updates, taken after the server update; each record a header (record,
world update, frame, tick, zone, hashes) and the fields that changed. `dump <name>` writes the whole state once. Groups:
- `world` (tick, clock, weather, alarms, zone flags, id map counts, diplomacy), `random` (ISAAC count / a / b / c and
  hashes; the main thread's rand() state), `player<id>` (money, items, members, variables), `obj<id>` (every server
  object: class, flags, place, effects, vitals; units also owner, relations, animation, movement, motion, controller and
  AI brain with modes by name). No pointer is written: objects by id, AI modes by name.
- digest 2: `timed<id>` (effects by class), `missile<n>`, `aievents` (the AI event table: always empty so far),
  `script` (instances, waiting runs, globals by name, FX sources, particles, lights, lightning; a randomized FX source's
  countdown is left out), `client` and `cunit<id>` (client units: place, orientation, animation, packed vitals, flags).
- Compact form: world, generators and players field by field; each unit as `#place`, `#vitals`, `#ai`, `#unit`; other
  objects one `#`; script one hash per kind (`#run`, `#inst`, `#global`, `#gobject`, `#fx`, ...). A golden compares only
  the groups it has, so an older digest still compares.
- Left out on purpose: a unit's motion floats before its first path (never written).
- Scenarios that load a save take `dump loaded<save>` once the load is done, so the loaded world is compared field by
  field.

**Saves** (tools/savefile.py with savefile_objects / _unit / _items / _players / _scenario.py; `show`, `diff`, `raw`,
`rules`): saves\<slot>\scenario.sav (magic 0xDEAD: party manager, interface, bottom window, camera), mission.sav (0x102:
WorldServer::SaveWorld), info.sav; shot.sav left out. A save file frames magic, version 0x74, checksum, a compressed flag
and the LZ coder's output. Every reader names the function it follows; a decode that does not end exactly at the data's
end is an error. Entities by id, fields by name in file order. A golden's saves.txt lists each save's path and the hash
of its decoded text; the texts are in the shared store tests/golden/saves/<hash>.txt.gz. No leave-out rule is in use:
every former leftover is canonical (section 5). A save made over the briefing or global map has a 4-byte bottom window
block. Not covered by the suite's saves: fake units, Tamed / Follow modes, armour spells, carried items, plan steps, most
timed effects, items 0x3006 / 0x3007, time-wheel events, script lights / particles / FX sources / areas, party manager
parties, saves\current\<zone>.sav.

### Recording goldens

Two passes of the build that must agree (`IMAGE_RUNS`). The Suites workflow does it on the runner (harness.md, "The
Suites workflow"): Run workflow on a branch (not main) with `record` = both / vanilla / rebirth, optionally `scenarios`
(space-separated) and `reason`; it pushes the goldens to the branch as a commit of its own and lists the new and changed
ones. That push starts no run: check the branch with a Suites run after it. By hand on the machine:
```
python tools\work.py test <ID> --golden [scenarios]       (each pass)
python tools\golden.py snapshot <scratch>\passN [--install rebirth]  (after each pass)
python tools\golden.py record --install vanilla --runs <scratch>\pass1 <scratch>\pass2 --reason "..."
python tools\golden.py saves --runs <dirs>         (only the saves of existing goldens, digests kept)
```
`record` keeps a scenario only when every run agrees with the first in console, every digest record, every dump and the
decoded saves; one that does not keeps its old golden. It names each golden it wrote new, the same as before (only
golden.json changed) or changed. Without a scenario list it re-records only the scenarios that have a golden on that
install already (and says how many runs it left out); a new golden needs the scenario named, or `--new` for every run
in the passes (a list-less record once wrote ~60 goldens nobody asked for, twice). Name the scenarios when recording. An intended change re-records the affected goldens and says why in the commit; anything
else that differs is a regression.

A golden that differs explains itself before anyone records it again (`tools/golden_explain.py`; `compare`, run_all
`--golden` and `golden.py explain A B` print it; `record` puts it in its note and the new golden.json's `change`, and
refuses without a `--reason` of a sentence). A summary is a headline (records that differ, the first one's world update,
tick, frame and zone, whether outcomes changed, the largest unit move), the first differing record, one line of
outcomes (zone, deaths, spawns, money, quest variables, party and items, game result, script: CHANGED or the same), the
sections (world, generators, players, units, objects, effects, missiles, AI events, script, client, water) with each
kind of field's entities, records and largest numeric change, and the saves by kind of record (obj, item, spell, ...).
Read it as: outcomes the same and unit moves of a few ULPs are the same game played with other rounding; a changed
outcome, a death or a move of whole units is a different game. Goldens recorded since H45 keep a unit's position, hit
points, mana and death in clear (`golden_explain.OUTCOME_CLEAR`, recorded as the golden's `clear`) so the summary has
numbers; older ones show "hash only" for units' place and AI. For every number, `golden.py explain <baseline run>
<new run>` (two full digests: keep the scenario's run folder from main's pass as the baseline) or
`golden.py record ... --before <baseline passes>`. No frame names: a digest record has no step, only its world update,
tick and frame.

Renderer-only scenarios (render_cursor_present, render_present_shot,
render_present_fullscreen) have goldens too, but their digests do not follow the renderer: the
world moves only with frames on the virtual clock, presents are counted on the main thread, a stall is one long frame.

`golden.py KNOWN` / `SAVES_KNOWN` (fields kept in clear but left out of comparisons until a ticket lands;
`compare --strict` keeps them) are empty.

### Scenarios without a golden

Every scenario in tests/scenarios has a vanilla golden but these (run_all's summary counts them as "without a golden"):
- **By rule**: the watchdogs (`watchdog_backstop`, `watchdog_dump`, `watchdog_main`: meant to time out); the
  `screenshots_*` (never in a suite, harness.md "Running a scenario"); the seven `render_device_lost_*` (a lost device
  ends the game with the renderer's fatal message: there is no passing end to compare).
- **Timing**: `render_weather_cost`, `effect_hooks_cost` (frame times measured on the machine).
- **Frame-checked renderer scenarios**: what they check is frames (tools/compare_frames.py or by eye; goldens hold no
  frames and renderer upgrades replace the look), over a world other goldens already play, several from a user's save
  whose decoded text is megabytes: `render_clouds`, `render_drawn_lights`, `render_drawn_motion`, `render_far_ground`,
  `render_load_town`, `render_map_clouds`, `render_occlusion`, `render_occlusion_cave`, `render_preview_blend`,
  `render_resize_edgepan`, `render_resize_zone`, `render_save_frame`, `render_shadows_decals`,
  `render_shore_seams`, `render_unit_skin`, `render_weather`,
  `render_weather_panel`, `render_weather_particles`, `render_weather_roofs`, `render_weather_shelter`,
  `render_weather_units`, `mod_lost_in_astral_first_pose` (R65's first drawn pose).

A Rebirth golden is made where Rebirth's data decides what a scenario checks (SV9: config/rebirth_scenarios.csv's
marks, Rebirth's steps, Lost in Astral, Rebirth's mods in the menu's list); every Rebirth golden puts its scenario into
the Rebirth set. The Rebirth set's scenarios without one are those above (frames, screenshots). A scenario
that gets a golden later is named on `record`'s command line.

### WideScreen references

tools/run_all.py `compare_widescreen`. The references are Rebirth's own WideScreen frames (game.exe with addon.dll),
recorded once and kept in `<main checkout>\build\widescreen\<scenario>\<steps key>\` (shared by the worktrees; the key
hashes the scenario's lines without comments, so a comment edit keeps them): `frames\<frame>.bmp.z`, `skip\<frame>.bmp.z`
(what moved in addon's run between the frame and its `_later` / `_moving` twins, grown by 8) and `recorded.json`. Not in
git (4K frames); no new recording can be made, as nothing runs game.exe any more. `run_all --widescreen [names]` runs
`<scenario>-standalone-rebirth` and compares each frame with `WIDESCREEN_COMPARE` (`--tolerance 8 --shift 1 --max-fraction
0.006 --block 64 --max-block 0.25`: `--shift 1` absorbs the renderers' sub-texel sampling and edges, `--max-block` catches a
moved element), leaving out the skip mask, what moved between our own twins and `WIDESCREEN_AREAS` (the Save window's
picture). Through `work.py test` the comparison runs after the batch, like `--golden`. A missing recording (other steps) or
another frame size fails; vanilla data cannot be compared (its textures differ from Rebirth's). A one-layout-unit shift of
the right panels (2.7 pixels) fails.

## 3. Coverage

Which of src/'s functions and lines the suites reach: `python tools/coverage.py run <ticket>` on the coverage build
(harness.md, "Source coverage"), a command run when wanted, not after each merge.
- Not reachable from a scenario: WinMain's second-instance exit, the low-disk-space box, Dib's error handlers (a failing
  disk). Dib::Read runs only for a loose `textures\<name>.bmp`.
- Dead: the fake unit (class 0x39): no shipped data makes one; the map object factory makes nothing for class 0x39
  (CreateObject returns null, as for an unknown class).
- Multiplayer pairs (section 6): `net_pair_kick` (kick by serial, ban by name,
  a refused join); `net_pair_leave` (shared files to a peer that lacks one, the peer lost in the game zone, the host back
  to the base: LeaveZone and its award); `net_pair_quest` (a prepared strong hero, `make_mp_hero.py`, finishes z9q2 and
  the sheriff's "_3" topic: CompleteZone and its awards; a public game whose `fakemaster` answers); `net_pair_master`
  (vanilla: the lobby without broadcasts asks the fake master three times, a bad size, a short record, the list it joins
  from; then `badupdate` gets it dropped as a bad client). Walks use `clickground` (the console's `#` / `@` commands do
  nothing on the multiplayer base), in the zone after a `lookat`.
- Still unreached: SuspectionMode Enter / Execute, patrol points, TrapObject::UpdateTrap, PickEnvironmentSpeech
  (the bored remark, 1 in 1001 per update after 500 bored ticks).

## 4. Run time

From the report's `TIMING:` lines (tests/verify/harness_timing.h: per frame the main thread's wall time and CPU in the
server's updates, the digest, the client's ticks, the steps, a pair's wait, the window checks and the rest; start-up to
the first frame; frames of 200 ms or more) and EI_PROFILE samples (`tools/profile_report.py`).
- `TIMING: gpu:` (gpu.h's `Waits`): per swap chain texture the main thread's wait for it (a frame in flight still on the
  GPU: the frame is the GPU's) and its time in the submissions; the download waits (captures, frame checks); the
  pipelines made after the start; the process's memory (Windows: commit charge and its peak) and the system's available
  memory. With a software device (WARP, lavapipe) the process's CPU above the main thread's is the device's work: on
  WARP a frame of the base bz1g took about 1.8 CPU-seconds and one of render_weather_shelter about 3, all but a few ms
  of it the wait for the swap chain texture (H62d).
- The suite's wall is net_pair_quest's (it starts first; the other runs fill the free slots). Median run 6-7 s; start-up
  about a second.
- A slot is for a game on the GPU: run_all gives it back when the game has ended and takes it again before a retry's game
  (work_sched.GameSlots; work-board.md). Frames are packed by a Python process of its own (frame_store.pack_run), outside
  the slot: packing in the runner's process held the GIL and slowed every other run's set-up.
- A frame on the virtual clock does not wait for vsync (Present(0, 0) on a flip-discard chain); most of it is the game's
  own drawing on the CPU.

## 5. What makes runs repeat

The harness makes the game deterministic and layout-independent. User decision: no golden may depend on heap layout,
timing or leftover memory; where the original read never-set or address-ordered data, ours does the canonical thing in
every build.

**Virtual clock** (tests/verify/harness_clock.h; default, `realtime` or `EI_HARNESS_CLOCK=real` to leave it): only the main
loop moves it, five world updates a frame; the main thread's sleeps and timed waits take no real time; sample ends fire on
virtual time; date and seeds fixed; steps run once a frame. Other threads read an anchor that each main-thread advance
moves to max(their time, the new virtual time), never back and never standing still (after a long load they stood still
and the cursor / music waits took seconds). `speed` speeds nothing up on it and no scenario uses it. While the
interface's close-all flag is set the main loop's peeks report no message (HoldMessagesForCloseAll), so the next pass
always does the close instead of whichever real message came first.

**Canonical values in our code** (each was a difference between runs of the original):
- Units' update countdown from `id & 0xF` (`UpdateCountdownFor`), not address bits.
- Unit statistics and vitals copies, server objects, map objects, Suspection / Fear modes, class templates, the view
  position, particle sources and lights start zeroed or set; path cells' speed word 0x200 and terrain bits 0.
- Saves write no padding or leftover bytes (a sound's name tail, the stock's codes, archive keys numbered by first save).
- A heard claim's point starts at 0,0,0.
- Script groups and object sets are ordered by object id (the script engine saves groups by id, and ids are unique while
  referenced; ties cannot happen); draw lists by texture / figure name, then depth (src/game/world/object_order.h).
- A client's player list is refreshed by its binding too (an update's objects come in the server's order; players before
  the binding were listed as nobody: the base screen's faces; the `checkplayers` step).
- The shadow's turn is undone exactly (no ulp drift per re-render).
- Client-only effects draw from their own generator (`g_clientRandom`, seeded from the game's at World::Create), not from
  ISAAC or rand(): terrain tiles and ambience, particles, lightning shapes, sound and speech picks, decals, the
  interface's flourishes, the client's idle animation picks and the hero previews' start frames. The particle sources
  have their own (`g_particleRandom`, seeded from the client's), so a particle change moves no other client draw. The
  server's animation picks keep rand().
- The terrain's sectors are lit once, when built.

**Behaviour that only looks random**:
- Any change in frame count reshuffles the ISAAC draws (the server's AI, weather and combat share the generator), so
  functions driven by them (zone1's storm, wolf pathing, the bored remark) come and go when a wait is cut. Cut waits only
  where coverage stays.
- rebirth_flags' `DistancePointPoint(3,4)` reads four floats past an 8-byte argument block in the original; ours reads 0
  for the two missing floats, and the scenario checks `Or(DistancePointPoint(3,4),1)`, true for every value.

## 6. Harness isolation

- **No foreground** (the harness's platform backend, tests/verify/platform/harness_platform.h): a harness game's
  window is never activated (shown without activation, minimised and restored by the focus steps without it); the
  harness tells the game about activation itself.
- **Out of sight** (Windows; `EI_HARNESS_SHOW_WINDOW=1` to watch): the game window is cloaked (DWMWA_CLOAK) and made a
  tool window before it is first shown (sys::ConcealWindow); frames, posted input and window size stay the same. The
  watch (sys::WatchWindows, from the frame loop at most every 200 ms) conceals any other top-level window of the process
  it finds in sight (a runtime box, a driver's window) and logs it. The report's `WINDOW:` line, run_all's
  `game windows:` summary. On Linux the window is shown (WSLg draws it).
- **The harness's input is the only input** (tests/verify/harness_input.h): the game reads keys, characters, the mouse and
  its motion from the harness's platform backend, which drops every system event; the steps post window messages, each
  made into the platform's event. Typed text follows the US layout with no key held
  (Windows' own translation followed the user's layout and held keys: typed names came out doubled). The report's
  `MOUSE:` line. The real cursor over the window once moved the camera and made runs not repeat.
- **Loopback** (tests/verify/harness_net.h `netlocal::`): a bind on every address binds 127.0.0.1, an unbound socket's
  datagram binds 127.0.0.1:0 first, a datagram outside loopback goes to 127.0.0.1 on the same port (the lobby's broadcast
  ping reaches the same game's server). Every game also gets a UDP port of its own instead of 8888 (games side by side
  otherwise joined each other's servers). The watchdog checks the process's endpoints every second; the report's `NET:`
  line, run_all's `network:` summary.
- **Pairs** (tests/verify/harness_pair.{h,cpp}): a scenario's `peer <name>` line makes run_all start a second game on
  tests/scenarios/peer/<name>.txt (one job object, either side's hang ends both; the peer's report in the run folder's
  peer\, merged into the host's as `FAILED: peer: ...`). The two run in lockstep, a turn at a time through a shared
  mapping, and their UDP sockets talk through a mailbox of virtual ports. A game's turn runs from its after-frame peek
  (steps, messages, the next pass's worlds and net::Pump, the interface's update) to just before the pass's frame step;
  the frame step (drawing and present) runs while the other game takes its turn. The frame step pumps the network too,
  so every datagram carries its sender's segment (a turn and the frame step after it); a receive takes datagrams of
  earlier segments or its own, oldest segment first; the frame step may send only to its own ports, to nobody (netcut)
  or to the fake master, and anything else there fails the run. `peersignal` / `peerwait` order the two scenarios;
  `netcut` pulls one side's cable; `hidefile <path>` makes an install file read as missing in one game until it writes it.
  `fakemaster <port> [list|badsize|short]`: datagrams to that virtual port are the master's (each announcement answered
  and kept), and a TCP connect to it gets the server list (or the bad replies); the report's `MASTER:` line. `nolan`: the
  game's broadcasts are lost; `badupdate`: its next update has its records replaced by an unregistered type, so the
  receiver drops it as a bad client. `netstate` writes the server's connection count and the lobby's join state and
  servers as console lines. A pair takes two machine slots; goldens keep both games (the golden's peer\).
- **Settings** (tests/verify/harness_settings.h): no run reads the user's settings. Each run gets its own
  `<run folder>\ei_settings.ini`, written at the settings' first use from the committed snapshot
  `config/harness/registry.reg` (the option tree on the run's install, Rebirth's switches as [rebirth]), with the
  scenario's starter lines (harness_starter.h: resolution, widescreen, fullscreen, music, mod) in place; read only,
  and a value the file lacks fails the run (`"name"=-` marks one read but absent on purpose). config\keyboard.ini and
  config\addrbook.ini are read from `config/harness/<vanilla|rebirth>/` unless the run's overlay has its own. Why: the
  user's own play once set the music volume to 0, and 13 goldens differed in random draw counts. To change a setting for
  every run, edit the snapshot and re-record what it moves; `EI_HARNESS_SETTINGS=<folder>` runs with another snapshot
  folder. `settingsfile writable | defaults` check the product's own file paths instead.
- **Music**: harness runs open no sound device, so the music's stream has no thread: the main loop steps it on the
  virtual clock, and the music system's random draws (a fight's combat track is picked only when the playing one is
  not it) come on the same frames in every run (sound.md, "Non-determinism"). Scenarios play with the music on.

### No run writes into an install

The game's file calls all pass one seam (platform.md, "One path seam"): the harness's SystemPath (harness.cpp's
MapPath) sends a write of an install file to the run's overlay (harness_saves\<relative path>; reads prefer the
overlay copy) and a save to the run's folder. A write whose path is still under an install root after that (the game
folder, EI_INSTALL_ROOTS: run_all's installs, `;`-separated on Windows and `:` elsewhere) is refused there: the path
maps to "", the call fails as on a missing file, and the run fails (`install: write refused: <path>`, with the caller's
address). A log (*.log) or a file over 16 MB is not copied into the overlay before a keeping write (the write starts
empty). mp\ (multiplayer heroes) is the overlay's only: a run starts with none, or a prepare tool's
(`make_mp_hero.py`).

What the seam does not see: another library's own file calls (the graphics driver's shader caches, SDL's), which go to
the system directly. Their paths are not the install's in practice (a driver's cache folder, the user's data folder),
and the runs' working directory is the install, so a library writing a relative path would write into it unseen.
tools/install_watch.py (H47a) looks for that from outside: run_all lists the installs the suite plays (every file's
name, size and time, read only; 0.1 s for both on Windows, 6.9k entries) before the first game and again after the last.
A run whose game window (run_once records it in `RUN_WINDOWS`) holds the time of an added or changed file, give or take
a few seconds, fails with `an install changed while this run played: <files>`; a removed file, or one whose time is older
than the first listing (copied in with its old time), has no place in time and fails every run of the suite; a change
inside no run's window is only printed. The summary says `install trees: N files ... no change`. `work.py test`
batches do the same in the scheduler (work_sched keeps one Watch per batch, listed when its first game starts).
Checked by tools/test_install_watch.py.

## 7. Diagnostic tools

- `EI_HARNESS_CLOCK=virtual,randtrace`: every random draw with its frame, caller and generator position (`#<n>`) in
  randtrace.txt, plus clock moves and sound ends; `tools/randtrace.py A B` reports the first draw B makes in no frame or
  another. `clocktrace`: every clock read and wait in ei-rebuild.log. Through `work.py test`, set it in the environment.
- `golden.py diff A B [--ulps N] [--ignore F] [--records K]`, `golden.py state A --record N`: where two runs' digests part,
  field by field; with `digest 1` and `randtrace` on the scenario this finds the first update and draw.
- `EI_WATCH_CUNIT=<id>[,flags]` (a client unit's orientation or flags writes, by debug registers), `EI_WATCH_ADDR=<hex>
  [,from,to]` (one dword's reads and writes), `EI_STACK_SCRUB`, `EI_SPIN_US` (tests/verify/harness_watch.h).
- `EI_SAVETRACE=1` (every memory stream write up to each saved file, savetrace.txt) with `tools/save_trace.py plain | diff
  A B` (the writers of differing save bytes).
- `EI_READTRACE=1`: every install path a run reads, lists or looks for, once each, in its harness_reads.txt ("+" found,
  "-" missing); `tools/stub_data.py reads <runs folder>` sums a suite's (linux.md, "The stub data").
- The Suites workflow's `diff_base` (harness.md) shows where two builds part.

## 8. Rules from real differences

- **Float grouping**: `/fp:fast` regrouped `(30x + 10) / 31` into `x * (30/31) + 10/31`, which settles on another fixed
  point (9.99999714 against 10.0). The project builds `/fp:precise`; float-maths.md has the rest.
- **Never-set memory** is made canonical, never emulated (section 5): our objects start zeroed or set. To find whether
  anything reads a field before its set, poison it right after its constructor (a temporary env-gated write; a quiet NaN
  and a finite 1e5), then run the `--golden` suite and hash the capture scenarios' frames against a plain pass. A
  positive control must move something.
- **Transform positions**: TransformObject's constructor leaves localPosition / worldPosition unset. Only the light read
  them unset (zeroed now); poisoned at every other owner, 175 goldens and 174 frames stayed the same: every effect's
  Reset zeroes the position, the terrain's reset and the sky set it, the camera reads its own only when placed, scene
  meshes and figure parts are zero-filled arrays, map objects and figures get theirs on placement / load.
- **Address order**: a sort or hash walk by pointer that reaches behaviour, a save or a frame gets an id or name order.
  Pointer-keyed containers that reach no output (MappedResource::filesByData, mod_folders' sets,
  ChunkArchive::numbers, text_sources' converted texts, the renderer's caches) need nothing. Net replication sends by
  entry serial (network.md, "Replication"). A golden that changes with the allocation order is a missing
  canonicalisation.
- **Comparators over floats** must stay irreflexive: a NaN key once sent an unguarded scan off the list.

## 9. Open

- unit_create parted once in crt.rand only (watch).
- A read-before-write detector for never-written heap bytes.
