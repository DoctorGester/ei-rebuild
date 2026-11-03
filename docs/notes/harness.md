# The harness

How to run scenarios, the scenario steps and what a run does. How changes are checked (goldens, WideScreen
references) is in `standalone-verification.md`.

## Running a scenario

`harness.cmd <scenario>` runs a scenario on `ei-rebuild.exe` with the vanilla install's data and exits with 0 when it
passes. It writes the report (`build\verify\harness_report.txt`): everything the game console printed, and any failed
checks. Scenarios live in `tests/scenarios/`; the steps are listed at the top of `tests/verify/harness.cpp`.

    harness.cmd tests\scenarios\sandbox_actions.txt

`python tools\run_all.py` runs every scenario on the vanilla install's data, several games at once (`--jobs N`,
default 16; the whole suite takes about 2 minutes); workers run it through `python tools/work.py test <ID> [run_all
args]` (work-board.md). Each run writes to its own folder, `build\verify\runs\<scenario>-<install>` (`ei_launch --out`,
`EI_OUT_DIR`): its `ei-rebuild.log`, report and `harness_saves`, emptied first. Installs: `standalone` (the vanilla
data) and `standalone-rebirth` (the Rebirth data and goldens, the patches of `config/rebirth_profile.ini` as
`EI_REBIRTH`; only on demand: `--rebirth` adds the smoke set of config/rebirth_scenarios.csv and the scenarios with
Rebirth-specific steps, `--rebirth-only` runs those alone, `--all-rebirth` every scenario). `EI_BUILD_DIR` points
run_all at another verify build.

A scenario with a line `# drop-in: <why>` (dropin_boot, dropin_profile) runs as a drop-in run: ei_launch copies the
build's exe into `<run folder>\dropin`, a folder made like the install, and starts it from the run folder with no
EI_GAME_ROOT (standalone.md, "The drop-in exe"). The line is read by ei_launch, so the scheduler's run_all (main's) runs
a worktree's drop-in scenarios with the worktree's ei_launch.

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
  camp view trade escmenu saveload options objectives help msgbox difficulty heroes titles`, or a vtable in hex. A new
  game's battle screen comes seconds after the leader has HP, so input for it waits on `waitwindow 15 battle` first.
  `waitunit <seconds> <name>` waits until `clickunit` would find the unit on screen. `frames <n>` lets the game run <n>
  frames (input posted before it has been handled after one). Plain `wait <ms>` is left for "let the world run this
  long". On the server world: `waitidle <seconds> [frames]` waits until the leader has stood idle for that many frames
  (default 10), `waitdead <seconds> <id>...` until those units are dead, `waitaward <seconds>` until the leader's
  experience or money changes (from an earlier `awardfrom`; `net_pair_quest.txt`).
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
- `peer <name>` makes a pair (`tests/verify/harness_pair.h`, `net_pair_join.txt`): ei_launch starts a second game on
  `tests\scenarios\peer\<name>.txt` beside this one, the two in lockstep on a network of their own (the second joins the
  first's hosted game over virtual UDP ports); `peersignal <tag>` / `peerwait <seconds> <tag>` order the two scenarios.
  The peer's report and logs are in the run folder's `peer\`; a pair takes two of run_all's slots. `netcut` cuts one
  game's network (`net_pair_leave.txt`); `hidefile <path>` makes an install file read as missing in that game until it
  writes the file itself.
- `rebirthflags <names>` turns on Rebirth patch flags for the run (run_all: `--rebirth all` or names; rebirth.md). A
  harness run never takes a game folder's profile: ei_launch sets `EI_REBIRTH=defaults` when the caller set none.
- `resolution <WxH|desktop>` and `widescreen 0|1` (or `EI_RESOLUTION` / `EI_WIDESCREEN`) set the run's window; without
  them runs play in a window of 960x720 on the vanilla data and 1280x720 on Rebirth's (each takes its goldens' shape;
  FullScreen 0 unless the scenario says `fullscreen 1`). A scenario with a `resolution` step runs alone on the machine.
  `chat 1|0` opens and closes the chat line. Every other setting a run reads comes from the committed snapshot
  `config/harness/` (`tests/verify/harness_settings.h`), never the user's own.

## The virtual clock

Harness runs use a virtual clock (`tests/verify/harness_clock.h`): each main-loop frame is five world updates, the main
thread's sleeps and timed waits take no real time, sound ends come on virtual time, the date and the random seeds are
fixed, and frames present without waiting for vsync. A scenario that repeats takes the same course on every run (a
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

## AddressSanitizer

`build.cmd asan` is the verify build with `/fsanitize=address` (every target; the runtime DLL,
clang_rt.asan_dynamic-x86_64.dll, copied beside them) into `build\asan`; the product builds never have it. Run the suite
on it with `set EI_BUILD_DIR=<checkout>\build\asan` then `python tools\work.py test <ID> --golden --jobs 4` (any run_all
arguments). The exe sends the reports to `asan.<pid>` in the run's folder (tests/verify/image/asan_report.cpp;
`ASAN_OPTIONS` still works for other options): the first heap error (use after free, double free, overflow of a heap,
stack or global block, a new / delete mismatch) ends the game with its report, symbolised from the PDB, and run_all fails
the run with the error and its first frames. For a flaky heap crash this finds the cause in one run: a freed block stays
poisoned in the quarantine, so the write that only sometimes hit live memory always reports. The whole suite takes
about 6 to 8 minutes at `--jobs 4`; the scenarios that wait on real time (frame counts, `render_stats`, `camera_smooth`,
`app_record`, `ui_compare`, the cursor and device-removed renders, the long pairs) run out of time there: judge those by
their `asan.*` files, not their result.

## What a run leaves alone

Harness runs need no clicks and leave the install alone (standalone-verification.md, "No run writes into an install"):
startup movies are skipped, the tutorial option is off, saves and `config\tutorial.ini` go to
`build\verify\harness_saves`, every other file the game writes under its folder goes to a copy-on-write overlay there
(reads prefer the overlay copy), settings are not written. Every game hosts and joins on a UDP port of its own instead of
8888 (but a pair's two games; `tests/verify/harness_net.h`) and binds on 127.0.0.1 only (the report's `NET:` line,
run_all's `network:` summary). The user's real mouse never reaches the game (`tests/verify/harness_input.h`, the
report's `MOUSE:` line; the harness's posted input and its GetCursorPos are the game's only mouse). For parallel runs the
harness ignores other game windows, keeps the game active when it loses focus, and opens data files for shared reading.
Harness games never take the foreground (`noforeground::`), and every window stays out of sight (cloaked tool windows;
`EI_HARNESS_SHOW_WINDOW=1` shows them to watch a run); the report's `WINDOW:` line and run_all's `game windows:` summary
check it. Harness runs open no sound device (sound.md).

## Hangs, crashes and timeouts

- A game that hangs on exit is ended 3 s after the scenario quits (where its threads were: `exit_hang.txt` in the run
  folder). Exit hangs come and go with the machine's load and are only a note. An empty `exit_hang.txt`, or one that
  lists only threads at `ntdll!RtlUserThreadStart` with no frames, means the process was still ending in the kernel.
- ei_launch's own output and exit code go to the run's `ei_launch.txt`; a report left at `RESULT: RUNNING` with no
  failure fails with its exit code.
- When the game crashes, the stack is in `ei-rebuild.log`; run_all prints it by function names under the run's line
  (outermost first) and `python tools\resolve_crash.py <run folder>` prints the named stack (`--raw`: every logged word).
- A run that exceeds its time limit logs where the main thread waits. No game outlives its limit: the watchdog thread in
  the game ends it at the scenario's `timeout` (60 s unless the scenario says more); if that thread cannot run, ei_launch
  ends the game 20 s later (and the game runs in a job object that dies with ei_launch, children included). run_all
  reports both as `TIMEOUT`; ei_launch exits 4 (or 5 when Windows could not end the process).
  `tests\scenarios\watchdog_*.txt` check this (`# expect: TIMEOUT`).

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
