# The harness

How to run scenarios, the scenario steps and what a run does. How changes are checked (goldens, WideScreen
references) is in `standalone-verification.md`.

## Running a scenario

`harness.cmd tests\scenarios\<name>.txt` runs one scenario on `ei-rebuild.exe` with the vanilla install's data
(`run_all.py <name> --vanilla-only --retries 0`) and exits with 0 when it passes. The report
(`build\verify\runs\<name>-standalone\harness_report.txt`) holds everything the game console printed, and any failed
checks. Scenarios live in `tests/scenarios/`; the steps are listed at the top of `tests/verify/harness.cpp`.

    harness.cmd tests\scenarios\sandbox_actions.txt

`python tools\run_all.py` runs every scenario on the vanilla install's data, several games at once (`--jobs N`,
default 16; the whole suite takes about 2 minutes); workers run it through `python tools/work.py test <ID> [run_all
args]` (work-board.md). Each run writes to its own folder, `build\verify\runs\<scenario>-<install>` (`EI_OUT_DIR`): its
`ei-rebuild.log`, report and `harness_saves`, emptied first. A folder that cannot be emptied (a file in it still open) fails the run; a run that passes in digest mode
(`EI_DIGEST`, `--golden`) without a `digest.txt` fails too, and a digest the game cannot create fails its report (H54). Installs: `standalone` (the vanilla data) and
`standalone-rebirth` (the Rebirth data and goldens; only on demand: `--rebirth` adds the Rebirth set,
`--rebirth-only` runs it alone, `--all-rebirth` every scenario). The Rebirth set is config/rebirth_scenarios.csv's
marks, the scenarios with Rebirth-specific steps and every scenario with a Rebirth golden (run_all.rebirth_scenarios),
so main's `--rebirth-only --golden` after each merge compares every Rebirth golden there is: one outside the set went
stale unseen after golden-moving merges (H51a found 66 of 102 behind C84b, H47c, C81 and C84d). A scenario gets a
Rebirth golden only to be checked on Rebirth's data after every merge; 102 runs take about a minute on 16 slots. The
Linux runner (linux.md) plays only the marked and stepped part (`rebirth_scenarios(goldens=False)`). The installs' folders are `EI_GAME_VANILLA` and `EI_GAME_REBIRTH` (default
`F:\games\EvilIslands`, `F:\games\Evil Islands (Rebirth)`; run_all.install_dir, also play.cmd's and the tools that read
game.exe, tools/exe_path.py); a `work.py test` batch carries the submitter's two, so a suite on the stub data
(linux.md, "The stub data") goes through the scheduler too. `EI_BUILD_DIR` points run_all at another verify build.

How a run starts (run_all.launch, tools/game_process.py): the exe itself, with the working directory and `EI_GAME_ROOT`
the install, `EI_HARNESS` the scenario, `EI_OUT_DIR` the run folder and `EI_INSTALL_ROOTS` both installs. No
`EI_REBIRTH`: a run takes the install folder's own profile, as a player's exe does (rebirth.md; a caller's `EI_REBIRTH`,
such as the Suites workflow's diff `flags`, replaces it on both installs). The game prints into the run folder's
`launch.txt`, followed by run_all's own lines (`run_all: ...`). The games run in a job object on Windows (a session of
their own with PR_SET_PDEATHSIG on Linux), so none outlives run_all.

The scheduler that `work.py test` feeds is the main checkout's code: it plays a worktree's build with main's run_all. A
branch that changes how run_all starts a game is checked with its own `python tools\run_all.py` (machine slots still
apply), not through the scheduler, until it is merged.

A scenario line `# prepare: <tool> [args]` runs `python tools\<tool> <harness_saves dir> [args]` before the game starts
(`tools/prepare_scenario.py`): the game sees those files under its `saves\` (e.g. `magic_traps.txt` and
`tools/make_trap_mob.py`).

## Scenario steps

- `newgame` starts a new single-player game (the hero on zone1); `load <slot>` loads a save.
- `console <command>` types into the game's console. `console thingamabob` unlocks scripting; then `@<command>` runs a
  world script command (e.g. `@AddUnitToServer(...)`, `@Attack(...)`) and `#<expression>` prints a script expression.
- `check <expression>` requires the expression to print nonzero; `expect`/`reject <text>` look for console output.
- `waitfor <seconds> <expression>` waits until the expression is nonzero (fails after <seconds>); prefer it to fixed
  `wait`s. `waitline <seconds> <text>` waits until the console has printed <text> (e.g. `Saved savea`).
- Waits on the interface rather than the clock (each fails the run after <seconds>): `waitmenu [seconds]` (default 30)
  waits until the main menu is up and taking input, and every scenario starts with it. `waitwindow <seconds> <window>`
  waits until that window is on top, nothing is queued to replace it, no game request (new game, load) is pending, and
  the save/load or options window has finished sliding in, for two frames in a row. Names: `menu battle briefing map
  camp view trade escmenu saveload options objectives help msgbox difficulty heroes titles mpconnect masterbox`. `ifwindow <window> <step>` runs a step only while that window is
  on top (else skipped, logged): a box that Windows keeps up and Linux closes by itself. A new
  game's battle screen comes seconds after the leader has HP, so input for it waits on `waitwindow 15 battle` first.
  `waitunit <seconds> <name>` waits until `clickunit` would find the unit on screen. `frames <n>` lets the game run <n>
  frames (input posted before it has been handled after one). Plain `wait <ms>` is left for "let the world run this
  long". On the server world: `waitidle <seconds> [frames]` waits until the leader has stood idle for that many frames
  (default 10), `walkto <seconds> <x> <y> [frames]` walks the leader to a map point (lookat, a ground double click,
  then idle for that many frames, default 20; clicked again when a fight stopped it short), `waitdead <seconds>
  <id>...` until those units are dead, `waitaward <seconds> [received]` until the leader's experience or money changes (from an
  earlier `awardfrom`; `net_pair_quest.txt`; `served`: instead until the game server's queued awards, each kept 60 s, are
  gone, `net_pair_leave.txt`). On the base screen: `waitdialogue <seconds> <state>` waits for its
  dialogue state (1 a person's topics open: `clickregion` picks one), `talk <seconds>` clicks through the dialogue's
  phrases and rewards until it is over (`net_pair_leave.txt`).
- `key <key>` and `click/rclick/move <x> <y>` send keyboard and mouse input to the game window (coordinates in the
  800x600 interface layout); Esc and mouse clicks reach the interface. `clickregion/rclickregion/dblclickregion/
  moveregion <n>` do the same at the centre of region `<n>` of the window on top (region numbers as the screen's code
  uses them; ui.md). `hold <mods>` / `release <mods>` press and release Ctrl / Shift / Alt (`ctrl shift alt`), and the
  same words after a key or mouse step's arguments (`click 400 560 shift`, `key 1 ctrl`) hold them around that step
  (`ui_modifier_keys.txt`). `clickunit <name> [l|r|move|dbl] [mods]` clicks the unit with that script name where the
  game's own picking finds it this frame (`dbl` sends only a double click's second half); `clickunit #<id>` picks a map
  object by id (a lever, `levers_key.txt`). `clickground <x> <y> [click|dblclick|rclick|move]` does that mouse step
  where the game's ground pick finds map point (x, y) (`net_pair_zone.txt`). `layout <mouse step> <x> <y>` aims at the
  800x600 layout's point in the window's centred 4:3 part (the plain mouse steps spread their points over the whole
  window); `checksave ... centre` takes the save picture's 4:3 part in a wider window.
- `screen <id>` asks the server for interface screen `<id>` as the global map's buttons do (4 the global map, 5 the
  camp); the camp's Esc returns to the game. `screen 5 <n>` opens the camp with constructor / trader class `<n>` as a
  base dialogue's "constrN" answer does (1, 3, 4, 5 after a new game; `trader_stock.txt`).
- `zone <name> [exit]` leaves the running zone as its exits do for map.txt zone `<name>` (`gz2g`, `bz1g`, ...), the
  party arriving at that zone's exit `<exit>` (from 0: map.txt's `#exit n` minus 1); `waitzone <seconds> <name>` waits
  until it is loaded. Starting a zone from the main menu crashes the game, so scenarios start with `newgame` and change
  zones from zone1 (`zone_gz2g_traps.txt`; the zone list in ui.md).
- `speed <ms>` sets the real time per world update (game default 55); on the virtual clock it speeds nothing up.
- `capture <name>` writes the next presented frame to `frames\<name>.bmp` in the run's folder; `freeze` / `unfreeze`
  hold the game's clocks and camera around captures; `samecapture <a> <b>` requires two captures to match,
  `diffcapture <a> <b> <least> <most>` that they differ in that share of the pixels; `blink faces|open|close` loads the
  units' faces or shows every face's open / closed state (render_blink). `python tools\compare_frames.py A B` compares
  two captures or two runs' frames with a tolerance (`--mask` restricts it to where two other frames differ). A
  finished run's frames are stored packed (`<name>.bmp.z`, tools/frame_store.py). `render <spec>` sets the renderer's
  options for the run (renderer.md).
- `peer <name>` makes a pair (`tests/verify/harness_pair.h`, `net_pair_join.txt`): run_all starts a second game on
  `tests\scenarios\peer\<name>.txt` beside this one, the two in lockstep on a network of their own (the second joins the
  first's hosted game over virtual UDP ports); `peersignal <tag>` / `peerwait <seconds> <tag>` order the two scenarios.
  The peer's report and logs are in the run folder's `peer\`; a pair takes two of run_all's slots. `netcut` cuts one
  game's network (`net_pair_leave.txt`); `hidefile <path>` makes an install file read as missing in that game until it
  writes the file itself.
- `rebirthflags <names>` turns on Rebirth patch flags for the run, on top of the install folder's profile (rebirth.md).
  `folderprofile` checks that profile itself (harness_folder_profile.h, `dropin_profile.txt`).
- `resolution <WxH|desktop>` and `widescreen 0|1` (or `EI_RESOLUTION` / `EI_WIDESCREEN`) set the run's window; without
  them runs play in a window of 960x720 on the vanilla data and 1280x720 on Rebirth's (each takes its goldens' shape;
  FullScreen 0 unless the scenario says `fullscreen 1`). A scenario with a `resolution` step runs alone on the machine.
  `chat 1|0` opens and closes the chat line. Every other setting a run reads comes from the committed snapshot
  `config/harness/` (`tests/verify/harness_settings.h`), never the user's own.
- `recordinput [live]` records the run as a scenario, `recorded` marks one, `at <frame> <step>` / `at <frame> event
  ...` are its lines, `realinput ...` is input made as SDL's own ("Recorded play" below).

## The virtual clock

Harness runs use a virtual clock (`tests/verify/harness_clock.h`): each main-loop frame is five world updates, the main
thread's sleeps and timed waits take no real time, sound ends come on virtual time, the date and the random seeds are
fixed, and frames present without waiting for vsync. The fixed date, 2001-03-26 12:00, comes with a fixed zone, UTC+2
(the process's `TZ` is set to `EET-2`, so the log's times of day are in it too): the main menu's time of day and a
save's time (info.sav) are then the same on a machine in any zone. A scenario that repeats takes the same course on every run (a
golden is recorded only from runs that agree). The harness's waits count virtual time, each with a real-time safety
limit. A `realtime` line puts a scenario on the real clock; `EI_HARNESS_CLOCK` (or `run_all.py --clock`) overrides:
`virtual` or `real`, plus `clocktrace` (every clock read and wait by function, caller and thread, in `ei-rebuild.log`)
and `randtrace` (every random draw with its frame and caller, in `randtrace.txt`; `tools/randtrace.py` finds where two
runs part). The heap moves its blocks in every run, so a golden never follows heap addresses. The trace tools
(`EI_SAVETRACE`, `EI_WATCH_*`) are in standalone-verification.md.

## Digest and goldens

The game-state digest (`tests/verify/harness_digest.h`): `digest <N>` (or `EI_DIGEST=<N>`) writes the world's state
(units, positions, vitals, effects, AI state, generators, players) every N world updates to the run's `digest.txt`,
`dump <name>` all of it once; `python tools\golden.py diff <run A> <run B>` names the first world update and field where
two runs differ, and `golden.py record` / `compare` keep and check golden runs in `tests/golden/` (recorded on the
runner by the Suites workflow's `record` input; standalone-verification.md, "Recording goldens"). `python tools\work.py
test <ID> --golden` compares every run that has a golden with it, its saves too, decoded field by field (`python
tools\savefile.py show|diff|rules`).

Unit checks (`tests/unit`, `ei_unit_check.exe`, run by build.cmd): the settings file's format and lib::HashMap's walk
order, without the game.

Timing runs: `EI_PROFILE=1` samples the main thread into the run's `profile.txt` (`python tools\profile_report.py <run
folder>`: time per function between harness steps).

## Recorded play

Real play becomes a scenario with a golden (`tests/verify/harness_record.h`, `tools/recorded.py`).

**To record a session by hand:** `play.cmd record <name>` (Rebirth's data: `play.cmd record <name> rebirth`; the
suites play every scenario on the vanilla data). It builds the verify build and starts it on
`tests/scenarios/record/live.txt` in a 960x720 window that is shown and takes the keyboard and mouse, from the main
menu on (no sound; no movies: below). Play; close the window, or Exit from the main menu, to end it. The session
becomes `tests/scenarios/rec_<name>.txt`. Keep it to about a minute (`recorded.py` warns past frame 3750): the replay
runs faster than the play, but a run has 60 s. Then replay it twice and record its golden like any new scenario
(standalone-verification.md, "Recording goldens"):

    python tools\work.py test <ID> rec_<name> --golden --vanilla-only      (twice, golden.py snapshot after each)
    python tools\golden.py record --install vanilla --runs <pass1> <pass2> --reason "..." rec_<name>

**What a recording holds.** A scenario line `recordinput` makes its run write `recorded.txt` in the run folder: the
marker `recorded`, the scenario's lines read before the game starts (window, settings, flags, `# prepare:`), then an
`at <frame> ...` line for everything that reached the game, at the harness's frame count: each platform event the
harness queued (`at 14 event down left 564 396 1 c`: keys with code, scan code, modifiers, `raw` when the harness adds
no character; text; mouse moves, buttons, wheel), the mouse's motion (`event motion`), the harness's mouse position
(`event cursor`, or `c` on the event that put it there), and the steps that act on the game themselves (`newgame`,
`load`, `console`, `zone`, `screen`, `focus`, `windowsize`, `frametime`, `quit`, ...). The input steps (`key`, `click`,
`clickunit`, `walkto`'s clicks, ...) leave only their events, the waits and checks nothing; any other step fails the
recording (record::Classify). On replay each `at` line runs in the pass after its frame (the steps run once a frame on
the virtual clock), so the game takes the same input at the same frames: a replay's digest equals its recording run's
(`golden.py diff` of the two). An `at` line whose frame has passed fails the run (the replay has parted). A recorded
scenario's game may quit by itself after its last line (the player's Exit), which passes.

**Live play** (`recordinput live`, live.txt): the backend takes SDL's keyboard and mouse events through the game's own
translation (`src/platform/sdl/sdl_input.h`, the same as platform_sdl.cpp's) and holds them until the pass after the
next frame, where they are queued and recorded (a frame's mouse moves merged into the last); the window is shown,
activated and not resizable, the cursor held for real (relative mode, as the game's backend). Frames are 16 ms of game
time (`frametime 16`) held to the wall clock, so the game plays at its speed; focus changes reach nothing (the game
stays active, as in every harness run). `realinput key|text|move|click|rclick` makes the same SDL events in a hidden
run (tests/scenarios/record/real_input.txt).

**Movies**: a recording and a recorded scenario's run play none. PlayMovie (the new game's intro, the exit's, a
script's) times its frames by the wall clock in a loop where the steps do not run, so the player's Esc would reach
nothing and the replay would wait for the whole movie: their files read as missing there (harness.cpp PlayedMovie);
the loading screen's movie (app::g_loadingMovieName) is drawn by the load's progress and stays. Other runs play them
(render_movie_present).

The recordings in the suite and their sources (`python tools\recorded.py take <run folder> <name>` turns a run's
recording into `rec_<name>.txt`): `rec_menu_newgame` (the main menu's clicks into a new game, a walk, camera keys and
wheel, the escape menu), `rec_save_load` (a save and its load through the escape menu's windows), `rec_real_input`
(`realinput` keys, text and clicks, the console, `focus off` / `on`), recorded from `tests/scenarios/record/*.txt`
(not in the suite; run by name, `record/<name>`). A change that moves a recording's game re-records its golden as any
other; when the menus or the windows' timing change, the recording may miss what it clicked: record it again from
its source.

## AddressSanitizer

`build.cmd asan` is the verify build with `/fsanitize=address` (every target; the runtime DLL,
clang_rt.asan_dynamic-x86_64.dll, copied beside them) into `build\asan`; the product builds never have it. Run the suite
on it with `set EI_BUILD_DIR=<checkout>\build\asan` then `python tools\work.py test <ID> --golden` (any run_all
arguments). The exe sends the reports to `asan.<pid>` in the run's folder (tests/verify/image/asan_report.cpp;
`ASAN_OPTIONS` still works for other options): the first heap error (use after free, double free, overflow of a heap,
stack or global block, a new / delete mismatch) ends the game with its report, symbolised from the PDB, and run_all fails
the run with the error and its first frames. For a flaky heap crash this finds the cause in one run: a freed block stays
poisoned in the quarantine, so the write that only sometimes hit live memory always reports. The whole suite takes
about 6 to 8 minutes at `--jobs 4`; the scenarios that wait on real time (frame counts, `render_stats`, `camera_smooth`,
`app_record`, `ui_compare`, the cursor and device-removed renders, the long pairs) run out of time there: judge those by
their `asan.*` files, not their result.

## Source coverage

`python tools\coverage.py run <ticket>` builds `build.cmd coverage`, plays the image and Rebirth suites on it (`work.py
test <ticket> --rebirth --golden`, with `EI_BUILD_DIR` at `build\coverage`; more run_all arguments pass through) and
writes `build\coverage\report.md` (totals, a table per area with gameplay first, the ranked gaps, the biggest unreached
functions, every file), the HTML view `build\coverage\html\index.html` (a page per file, its lines marked) and
`coverage.json`. `python tools\coverage.py report` rewrites them from the runs already there. Not part of the merge
routine: run it when wanted.
- **The build** (CMakeLists.txt's EI_COVERAGE): the verify build's ei-rebuild.exe (only that target; no build checks)
  with src/ compiled with MSVC's `/fsanitize-coverage=inline-bool-flag` (a flag byte set on each instrumented edge) and
  `/Gh` (each function calls `_penter` once at its start: tests/verify/harness_coverage_entry.asm marks the function's
  16-byte slot of the image, then turns the call into a 2-byte jump over it, so only a function's first entry costs),
  at `/Ob1` and with `/OPT:NOREF` (functions nothing calls stay, as unreached). The harness writes the run's flags and
  marked slots to `coverage.dat` in the run folder at each report and at exit (tests/verify/harness_coverage.cpp); a
  pair's peer writes its own into `peer\`.
- **Why MSVC's and not clang's:** the Build Tools here have no clang-cl (only clang-format and clang-tidy under
  VC\Tools\Llvm), so llvm-cov's source-based coverage would need the "C++ Clang tools" component or an LLVM install;
  MSVC's options need nothing and keep the compiler the goldens come from. Their limits, found with small test files:
  `/fsanitize-coverage=pc-table` (the edges' addresses) and `no-prune` crash the compiler (C1001) on most of our files;
  `func` and `edge` exclude each other (the last one wins); the default edge mode prunes so hard that a function whose
  path was entry, one loop pass and the exit sets no flag, so entries come from `/Gh` instead.
- **`_penter` keeps the flags:** MSVC places the call inside the prologue, sometimes between a `test` and the `cmov`
  that reads it (terrain_sector.cpp's BuildRecords took the wrong layer count and crashed every run until `_penter`
  saved them with pushfq).
- **The report** (tools/coverage.py): dumpbin's disassembly of the exe gives each instrumented function (a label whose
  code calls `_penter`), its basic blocks (cut at jumps, returns and jump targets) and which block each flag's `mov byte
  ptr [flag],1` is in; dbghelp gives the PDB's line records. A function is reached when its slot was marked; a line when
  its function was and some of its code is outside the blocks whose flags stayed clear. Blocks without a flag count with
  their function, so the line numbers are an upper bound and the function numbers exact. Only src/ counts, by the part of
  the PDB's path from `src/` on (the compiler cache's objects name the checkout that compiled them first); functions
  marked inline are inlined at `/Ob1` and count in their callers. `static.pickle` keeps the exe's analysis.
- **Measured (H43, 2026-11-04):** the 258 runs (270 games with the pairs' peers) took 68 s, as on the verify
  build (the `_penter` patch), with every golden the same (252 the same, 6 without one); functions 7973 of 12292
  reached (64.9%), lines 66862 of 76533 (87.4%). The report itself takes under 10 s (dumpbin and dbghelp; then
  `static.pickle`). The mixer (src/audio) is never reached: harness runs open no sound device.

## What a run leaves alone

Harness runs need no clicks and leave the install alone (standalone-verification.md, "No run writes into an install"):
startup movies are skipped, the tutorial option is off, saves and `config\tutorial.ini` go to
`build\verify\harness_saves`, every other file the game writes under its folder goes to a copy-on-write overlay there
(reads prefer the overlay copy), settings are not written; run_all lists the installs' files before a suite and after
(install_watch.py) and fails the runs that overlap a change. Every game hosts and joins on a UDP port of its own instead of
8888 (but a pair's two games; `tests/verify/harness_net.h`) and binds on 127.0.0.1 only (the report's `NET:` line,
run_all's `network:` summary). The user's real mouse never reaches the game (`tests/verify/harness_input.h`, the
report's `MOUSE:` line; the harness's posted input and its GetCursorPos are the game's only mouse). For parallel runs the
harness ignores other game windows, keeps the game active when it loses focus, and opens data files for shared reading.
On Windows harness games never take the foreground and every window stays out of sight (the game window concealed before
it is first shown, any other window of the process concealed when the watch finds it; `EI_HARNESS_SHOW_WINDOW=1` shows
them to watch a run); the report's `WINDOW:` line and run_all's `game windows:` summary check it. On Linux the window is
shown on the game's display; run_all starts one Xvfb for the suite (`start_headless_display`: a free number from :99 down, a 3840x2160 screen
(the goldens' desktop: the `resolution desktop` scenarios, ui_widescreen_*, play differently on a smaller one),
every game's DISPLAY pointed at it, WAYLAND_DISPLAY unset, stopped when run_all ends), so a local run opens nothing on the
desktop; `EI_SHOW_WINDOWS=1` leaves the games on the session's display to watch them. Harness runs open no sound device (sound.md).

## The harness's code

tests/verify: harness.cpp (the steps, the overlay, the report) and its parts; image/ (what reads the game's objects);
platform/ (the platform backend: platform.h's functions the harness's builds take in place of the game's,
harness_platform.cpp on SDL and game_answers.cpp for the calls a run answers; platform.md, "The harness's answers"); and
system/ (the harness's own system calls, harness_system.h with system_win32.cpp and system_posix.cpp, the sockets'
helpers, and Windows' sampling profiler and debug-register watch). CMakeLists.txt builds the `_win32` or `_posix` files
for its system. The harness patches nothing of the system: no import slot, hot patch or registry answer (nothing in the
program reads the registry; the settings are a file, harness_settings.h). The harness builds on Linux
(`-DEI_HARNESS=ON`; linux.md).

## Hangs, crashes and timeouts

- A game that hangs on exit is ended 3 s after the scenario quits (where its threads were: `exit_hang.txt` in the run
  folder). Exit hangs come and go with the machine's load and are only a note. An empty `exit_hang.txt`, or one that
  lists only threads at `ntdll!RtlUserThreadStart` with no frames, means the process was still ending in the kernel.
- The game's output and run_all's lines for the run go to its `launch.txt`; a game that ends while its report still
  says `RESULT: RUNNING` gets a failure with its exit code (an INT3's named as such), and a harness that has not written
  its report 30 s after the start ends the run without one. A scenario's `# settings-file:` line is checked after the
  game has ended (run_all.check_settings_file).
- When the game crashes, the stack is in `ei-rebuild.log`; run_all prints it by function names under the run's line
  (outermost first) and `python tools\resolve_crash.py <run folder>` prints the named stack (`--raw`: every logged word).
- A run that exceeds its time limit logs where the main thread waits. No game outlives its limit: the watchdog thread in
  the game ends it at the scenario's `timeout` (60 s unless the scenario says more); if that thread cannot run, run_all
  ends the game 20 s later (and the game runs in a job object that dies with run_all, children included). run_all
  reports both as `TIMEOUT` (and says when the system could not end the process).
  `tests\scenarios\watchdog_*.txt` check this (`# expect: TIMEOUT`); `tools/test_run_all_timeout.py` checks run_all's
  side with a Python stand-in for the game.

## The Suites workflow

`.github/workflows/suites.yml` builds and plays the goldens on the self-hosted runner (Kirill's machine, labels
`self-hosted`, `ei-game`), which has the game installs and a GPU. Cloud sessions have no compiler or game data and test
through it.
- **Trigger**: a push to main or any `claude/**` branch, or by hand (Actions > Suites > Run workflow, or the API) with
  `ref` and `suites` = `full` | `quick` (quick: the verify and release builds plus the image suite; full: also the
  Rebirth suite, and on main the debug, asan and play builds). Never on pull_request.
- **Reading results**: the run's summary page has each suite's goldens block; a failing build prints its last 40 log
  lines in the step log. From a session: `gh run list --workflow suites.yml`, `gh run view <id> --log-failed`, or the REST
  API (`/repos/DoctorGester/ei-rebuild/actions/runs/<id>/jobs`, then `/actions/jobs/<id>/logs`). To wait for a run, poll
  `https://api.github.com/repos/DoctorGester/ei-rebuild/actions/workflows/suites.yml/runs` from a background command.
- **Where two builds part** (`diff_base`, with optional `scenarios`, `resolution`, `flags`, `records`, `skip`): plays the
  scenarios on both installs with the branch's verify build and with the base ref's and prints, for each run that
  differs, the first differing digest record field by field, the console lines, the failures and the window size. Use it
  before re-recording anything: every changed golden needs a cause.
- **Recording goldens** (`record` = both, vanilla or rebirth, optionally `scenarios` and `reason`; on a branch, not
  main): two passes that must agree, `tools/golden.py record`, and the goldens pushed to the branch as a commit of their
  own. That push starts no run, so dispatch a run on the branch after it.
- **Limits**: one run at a time (concurrency group `ei-game`); games share the machine's 16 slots with local work
  (through its scheduler); the runner is only up while Kirill is logged in; a push to a `claude/**` branch costs a full
  run (about 20 to 30 minutes), so batch commits.
- **Without the runner** a session can run the Python tools' own tests (`python -m unittest` in `tools/`) and the text
  checks that read only the source: `tools/check_cleanup.py`, `tools/comments.py check`, `tools/tickets.py`,
  `tools/data_seam.py --check`, `tools/named_data.py --check`, `tools/check_platform.py`,
  `tools/gen_settings_doc.py --check`. Linux builds there too (linux.md).
- A change that should not alter behaviour keeps every golden the same; an intended change re-records the affected
  goldens and says why in the commit.
