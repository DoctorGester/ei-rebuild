# Evil Islands (Проклятые Земли) rebuild

Readable C++ source for the Russian `game.exe` 1.06, rebuilt with modern MSVC (x86). The goal is
identical behaviour, not identical bytes.

The product is the standalone image: `ei_standalone.exe` lays out the original `game.exe`'s headers and
data without its code and loads `ei_hook.dll`, which writes a jump to each rewritten function at its original
address (docs/notes/standalone.md). The original `game.exe` runs only as the original game, for the harness's
references (goldens, function vectors, Rebirth's frames).

## Build

Requires the VS 2022 Build Tools (MSVC 14.43, x86) at `E:\bin\vs\buildtools` (edit `build.cmd` if elsewhere).

```
build.cmd release    # the product: the standalone image (ei_standalone.exe, ei_hook.dll)
build.cmd verify     # the harness build: the same image plus the harness (tests/verify) and the
                     # vector recorders (tests/vectors); what harness.cmd, run_all and work.py run
```

## Run

```
launch-vanilla.cmd
```

Starts the standalone image (`build\verify\ei_standalone.exe`: `run\vanilla`'s `game.exe` laid out without its
code, our code from `ei_hook.dll`) on the GOG install's data in `F:\games\EvilIslands`, with our renderer
(`ei_launch --standalone`, `EI_RENDER=new,present`). Nothing in the game folder is modified. The log is written next
to the DLL: `build\<config>\ei_hook.log`. `ei_launch` without `--standalone` starts the original `game.exe`
with `ei_hook.dll` (the harness) and `EI_NO_HOOKS=1`: none of our code replaces the original there
(docs/notes/standalone.md section 5).

## Layout

- `src/game/` rewritten game code. Each function notes its original address.
- `src/hook/` the replacement framework (`include/ei/hook.h`): the jumps into the image, the image's
  start (`standalone.cpp`), and in game.exe the vector recorders, coverage and profiler.
- `src/render/` the new renderer (sokol_gfx on Direct3D 11, `third_party/sokol`): the image always draws and
  presents with it (`EI_RENDER=new,present`, the default; `docs/notes/renderer.md`).
- `tests/verify/` the harness (scenario runs of the image and of the original game), compiled into the `verify` build.
- `tools/` launcher, Ghidra export (`export_functions.py`), PE inspection (`pe.py`).
- `config/functions.csv` every function in game.exe with kind (game/lib/eh/thunk) and status.
- `docs/` cleanup plan, rewrite guide, ticket queue and per-area notes (`docs/notes/`).

### Vanilla (no Rebirth)

`launch-vanilla.cmd` runs the image on the plain GOG install in `F:\games\EvilIslands`. On first use, `tools\setup_vanilla_run.cmd` builds `run\vanilla`: the GOG `game.exe` with Rebirth's dgVoodoo DLLs beside it (the original code's harness runs draw through them). `run\` is not committed. The game's shared registry paths are redirected to the GOG folder for that process only.

### Automated harness

`harness.cmd <scenario>` runs a scenario on the image with the vanilla install's data (`--no-hooks`: on game.exe
with the original code) and exits with 0 when it passes. It writes the report (`build\verify\harness_report.txt`):
everything the game console printed, and any failed checks. Scenarios live in `tests/scenarios/`; the steps are
listed at the top of `tests/verify/harness.cpp`. The main ones:

- `newgame` starts a new single-player game (the hero on zone1); `load <slot>` loads a save.
- `console <command>` types into the game's console. `console thingamabob` unlocks scripting; then
  `@<command>` runs a world script command (e.g. `@AddUnitToServer(...)`, `@Attack(...)`) and
  `#<expression>` prints a script expression.
- `check <expression>` requires the expression to print nonzero; `expect`/`reject <text>` look for
  console output.
- `waitfor <seconds> <expression>` waits until the expression is nonzero (fails after <seconds>);
  prefer it to fixed `wait`s.
  `waitline <seconds> <text>` waits until the console has printed <text> (e.g. `Saved savea`).
- Waits on the interface rather than the clock (each fails the run after <seconds>):
  `waitmenu [seconds]` (default 30) waits until the main menu is up and taking input, and every
  scenario starts with it. `waitwindow <seconds> <window>` waits until that window is on top, nothing is
  queued to replace it, no game request (new game, load) is pending, and the save/load or options
  window has finished sliding in, for two frames in a row. Names: `menu battle briefing map camp
  view trade escmenu saveload options objectives help msgbox difficulty heroes titles`, or a
  vtable in hex. A new game's battle screen comes seconds after the leader has HP, so input for it
  waits on `waitwindow 15 battle` first. `waitunit <seconds> <name>` waits until `clickunit` would
  find the unit on screen. `frames <n>` lets the game run <n> frames (input posted before it has been
  handled after one). Plain `wait <ms>` is left for "let the world run this long".
- `key <key>` and `click/rclick/move <x> <y>` send keyboard and mouse input to the game window
  (coordinates in the 800x600 interface layout); Esc and mouse clicks reach the interface.
  `clickregion/rclickregion/dblclickregion/moveregion <n>` do the same at the centre of region `<n>` of the
  window on top (region numbers as the screen's code uses them; see `docs/notes/ui.md`).
  `hold <mods>` / `release <mods>` press and release Ctrl / Shift / Alt (`ctrl shift alt`), and the same words after
  a key or mouse step's arguments (`click 400 560 shift`, `key 1 ctrl`) hold them around that step; the game
  takes them from the window messages, so the original and the image see the same (`ui_modifier_keys.txt`).
  `clickunit <name> [l|r|move] [mods]` clicks the unit with that script name where the game's own
  picking finds it this frame (`ui_battle_commands.txt`); `clickunit #<id>` picks a map object by id (a lever,
  `levers_key.txt`). `clickground <x> <y> [click|dblclick|rclick|move]` does that mouse step where the game's
  ground pick finds map point (x, y) (a walk order: `net_pair_zone.txt`).
- `screen <id>` asks the server for interface screen `<id>` as the global map's buttons do (4 the
  global map, 5 the camp); the camp's Esc returns to the game. `screen 5 <n>` opens the camp with
  constructor / trader class `<n>` as a base dialogue's "constrN" answer does (1, 3, 4, 5 after a
  new game; the first opening per class generates its trader stock, `trader_stock.txt`).
- `zone <name> [exit]` leaves the running zone as its exits do (the server's LeaveToZone for the
  local player) for map.txt zone `<name>` (`gz2g`, `bz1g`, ...), the party arriving at that zone's
  exit `<exit>` (from 0: map.txt's `#exit n` minus 1); `waitzone <seconds> <name>` waits until it is
  loaded. Starting a zone from the main menu crashes the game, so scenarios start with `newgame`
  and change zones from zone1 (`zone_gz2g_traps.txt`; zone list in `docs/notes/ui.md`, section 10).
- `speed <ms>` sets the real time per world update (game default 55). On the virtual clock it speeds
  nothing up (a frame is five updates whatever their length) and no scenario uses it; a scenario that
  needs the world to do something waits for it (`waitfor`) or waits long enough at 55 ms an update.
- `capture <name>` writes the next presented frame to `frames\<name>.bmp` in the run's folder;
  `freeze` / `unfreeze` hold the game's clocks and camera around captures; `samecapture <a> <b>`
  requires two captures to match, `diffcapture <a> <b> <least> <most>` that they differ in that share of
  the pixels; `blink faces|open|close` loads the units' faces or shows every face's open / closed state (render_blink).
  `python tools\compare_frames.py A B` compares two captures or two
  runs' frames with a tolerance (`--mask` restricts it to where two other frames differ).
  A finished run's frames are stored packed (`<name>.bmp.z`, tools/frame_store.py; compare_frames and
  frame_preview read both forms).
  `render <spec>` sets the renderer spec for a run of the image (ignored in a run of the original code;
  `docs/notes/renderer.md`).
- `peer <name>` makes a pair (`tests/verify/harness_pair.h`, `net_pair_join.txt`): ei_launch starts a second game
  on `tests\scenarios\peer\<name>.txt` beside this one, the two in lockstep on a network of their own (the second
  joins the first's hosted game over virtual UDP ports); `peersignal <tag>` / `peerwait <seconds> <tag>` order the
  two scenarios. The peer's report and logs are in the run folder's `peer\`; a pair takes two of run_all's slots.
  `netcut` cuts one game's network (a client lost, `net_pair_leave.txt`); `hidefile <path>` makes an install file
  read as missing in that game until it writes the file itself.
- Harness runs use a virtual clock (`tests/verify/harness_clock.h`): each main-loop frame is five
  world updates, the main thread's sleeps and timed waits take no real time, sample ends come on
  virtual time, the date and the random seeds are fixed, and frames present without waiting for
  vsync. A scenario that repeats takes the same course on every run, so
  `python tools\compare.py <scenarios> --runs 3` can diff the console output of the original code and
  the image (those that do not repeat yet are listed in `docs/open-questions.md`). The harness's waits
  (`wait`, `waitfor`, `frames`, ...) count virtual time, each with a real-time safety limit. A
  `realtime` line puts a scenario on the real clock; `EI_HARNESS_CLOCK` (or `run_all.py` /
  `compare.py --clock`) overrides: `virtual` or `real`, plus `clocktrace` (every clock read and wait
  by function, caller and thread, in `ei_hook.log`) and `randtrace` (every random draw with its frame
  and caller, in `randtrace.txt`; `compare.py --clock virtual,randtrace` to find where two runs part).
  On the virtual clock the CRT heap also gives the same addresses every run, and the harness makes
  the original's address- and leftover-dependent behaviour canonical (`lfheap` keeps the game's own
  heap). `EI_HEAP_JITTER=<seed>[,fill]` moves the heap's blocks (game.exe's and the image's) without
  changing the calls, to find behaviour that follows addresses. These and the trace tools (`heaptrace`,
  `EI_HEAP_FILL`, `EI_SAVETRACE`, `EI_WATCH_*`) are in docs/notes/standalone-verification.md, sections 5 and 7.
- The game-state digest (`tests/verify/harness_digest.h`): `digest <N>` (or `EI_DIGEST=<N>`) writes the
  world's state (units, positions, vitals, effects, AI state, generators, players) every N world updates to
  the run's `digest.txt`, `dump <name>` all of it once; `python tools\golden.py diff <run A> <run B>` names
  the first world update and field where two runs differ, and `golden.py record` / `compare` keep and check
  golden runs of the original code in `tests/golden/` (docs/notes/standalone-verification.md, section 2). The
  suite checks them with `python tools\work.py test <ID> --golden` (any run_all arguments): every run that has
  a golden is compared with it, its saves too, decoded field by field (`python tools\savefile.py
  show|diff|rules`). A scenario the original cannot run (its `# no original:` line) gets the image's own golden
  instead (`golden.py record --image`).
- Function vectors (`tests/vectors/vectors.h`): with `EI_VECTORS=1` the verify build runs the original of
  each function that has a recorder (`tests/vectors/record_<family>.cpp`) and writes a sample of its calls
  to the run's `vectors/` folder (`set EI_VECTORS=1` then `python tools\work.py test <ID> <scenarios> --no-hooks
  --vanilla-only`: the original code with the recorders as the only hooks); `python tools\vectors.py merge <run folders>` keeps them in
  `tests/vectors/data`, and `ei_vectors.exe` (run by build.cmd) replays them against our code without the
  game (docs/notes/standalone-verification.md, section 3).
- Coverage (`tools/coverage.py`): `tests/coverage/reach.txt` holds which scenarios reached each rewrite in
  the last hook build's runs; `coverage.py list` writes `tests/coverage/coverage.csv` (golden, vector, suite
  only or unreached, with a risk rank) and `coverage.py top` lists the largest gaps.

A scenario line `# prepare: <tool> [args]` runs `python tools\<tool> <harness_saves dir> [args]`
before the game starts (`tools/prepare_scenario.py`, called by harness.cmd and run_all.py): the
game sees those files under its `saves\` (e.g. `magic_traps.txt` and `tools/make_trap_mob.py`).

Timing runs: `EI_PROFILE=1` samples the main thread into the run's `profile.txt` (`python tools\profile_report.py
<run folder>`: time per function between harness steps; the image's own code shows as `ours:`, S21f), and `EI_BUILD_DIR` points
run_all at another verify build.

`--no-hooks` (harness.cmd, run_all, `ei_launch`) runs the same scenario on the original code only, as a baseline. Add
`--rebirth all` (or patch names, comma-separated) to turn on the Rebirth patch flags in the rewritten
code (`include/ei/rebirth.h`, `docs/notes/rebirth.md`); `ei_rebirth.ini` next to the DLL does the same
permanently, and a scenario can ask with `rebirthflags <names>`. A scenario's `resolution <WxH|desktop>`
and `widescreen 0|1` (or `EI_RESOLUTION` / `EI_WIDESCREEN`) answer the starter's settings the game and
Rebirth read for the run, without writing them (`tests/verify/harness_starter.h`; every other game setting a run
reads comes from the committed snapshot `config/harness/`, never the user's registry or key bindings:
`tests/verify/harness_settings.h`; `python tools\run_all.py
--widescreen` compares the image's frames on the Rebirth install with addon's WideScreen, recorded once with
`--widescreen --record` into `build\widescreen` of the main checkout by the scenario's steps, `docs/notes/rebirth.md`
and docs/notes/standalone-verification.md, "WideScreen references"; `chat 1|0` opens and closes the
chat line, which a single player game does not show). Harness runs
need no clicks and leave the install alone: startup movies are skipped, the tutorial option is off,
saves and `config\tutorial.ini` go to `build\verify\harness_saves`, every other file the game writes under
its folder (the composed-skin cache `textures\*.mmp`, `config\keyboard.ini`, `config\addrbook.ini`,
`mp\*.mp`, ...) goes to a copy-on-write overlay there (reads prefer the overlay copy), settings are not written to the
registry, every game hosts and joins on a UDP port of its own instead of 8888 (but a pair's two games; `tests/verify/harness_net.h`: games run
side by side otherwise join each other's servers) and binds on 127.0.0.1 only, its datagrams going nowhere else (so Windows
Firewall never asks; the report's `NET:` line, run_all's `network:` summary), the user's real mouse never reaches the game
(no client mouse messages from the cursor over its window, no DirectInput motion or buttons: `tests/verify/harness_input.h`,
the report's `MOUSE:` line; the harness's posted input and its GetCursorPos are the game's only mouse), Rebirth's starter switch CtfMon is answered off (addon.dll would otherwise close the user's ctfmon at start and
restart it from its DLL_PROCESS_DETACH, which hung every exit), and a game that hangs on exit is ended 3 s after
the scenario quits (where its threads were: `exit_hang.txt` in the run folder). When the game crashes,
the stack is in `ei_hook.log`; run_all prints it by function names under the run's line (`stack: ... -> ComposeTexture -> CFile::Write -> CxxThrowException`, outermost first, stale words dropped) and `python tools\resolve_crash.py <run folder>` prints the named stack (`--raw`: every logged word). A run that exceeds its time
limit logs where the main thread waits (instruction pointer and return addresses, as module+offset). No game outlives its limit:
the watchdog thread in the game ends it at the scenario's `timeout` (60 s unless the scenario says more); if that
thread cannot run, `ei_launch` ends the game 20 s later (and the game runs in a job object that dies with
`ei_launch`, children included). `run_all` reports both as `TIMEOUT`; `ei_launch` exits 4 (or 5 when Windows could not
end the process: a thread stuck in a driver call). `tests\scenarios\watchdog_*.txt` check this (`# expect: TIMEOUT`).

    harness.cmd tests\scenarios\sandbox_actions.txt

`python tools\run_all.py` runs every scenario on both installs' data, several games at once (`--jobs N`,
default 16; the whole suite takes about 2 minutes). These are runs of the standalone image (below);
`--no-hooks` runs game.exe with the original code (but on the installs a scenario's `# no original: vanilla
rebirth` line names: checks only our renderer passes, Rebirth's flags on the vanilla data). Nothing runs our code
inside game.exe (docs/notes/standalone.md section 5). Each run writes to its own folder,
`build\verify\runs\<scenario>-<install>` (`ei_launch --out`, `EI_OUT_DIR`): its `ei_hook.log`,
report and `harness_saves`, emptied first, so every run starts without saves or texture cache.
For parallel runs the harness ignores other game windows (the game otherwise exits when it finds
one), keeps the game active when it loses focus (it otherwise pauses), and opens data files for
shared reading. The image draws with our renderer (`EI_RENDER=new,present`, no display mode
change, so none of its runs needs the machine to itself); dgVoodoo draws the original code (`--no-hooks`,
`--widescreen --record`'s addon runs, compare.py's original side). Passed runs of the original code are cached in
`build\reference` by scenario commit and install (`--fresh` plays them again; the key leaves out the harness).
The standalone runs (the default, or `--standalone`) start `ei_standalone.exe` instead of game.exe (install
`standalone`: run\vanilla's game.exe laid out without its code, the vanilla data, our renderer; `ei_launch
--standalone`) and on the Rebirth install (`standalone-rebirth`: run\rebirth's game.exe, the Rebirth data and
goldens, the flags of `config/ei_rebirth_standalone.ini`; only on demand: `--rebirth` adds the smoke set of
config/rebirth_scenarios.csv and the scenarios with Rebirth-specific steps, `--rebirth-only` runs those alone,
`--all-rebirth` every scenario); an address with no rewrite yet ends the run with `standalone: no code at ...` (docs/notes/standalone.md).
The vanilla runs of the original carry Rebirth's bug fixes, the standalone's defaults (docs/notes/rebirth.md, "One test profile").
Harness games never take the foreground (harness.cpp, `noforeground::`), and every run (the image's and the
original's) keeps every window out of sight (cloaked tool windows: no window, taskbar button or Alt+Tab entry; `EI_HARNESS_SHOW_WINDOW=1` shows
them to watch a run); the report's `WINDOW:` line and run_all's `game windows:` summary check it
(docs/notes/standalone-verification.md, section 6).
