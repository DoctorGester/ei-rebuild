# Evil Islands (Проклятые Земли) rebuild

Readable C++ source for the Russian `game.exe` 1.06, rebuilt with modern MSVC (x86). The goal is
identical behaviour, not identical bytes.

While the rewrite is incomplete, `ei_hook.dll` is loaded into the original `game.exe` and each
rewritten function replaces the original with a jump. Everything else still runs the original code.

## Build

Requires the VS 2022 Build Tools (MSVC 14.43, x86) at `E:\bin\vs\buildtools` (edit `build.cmd` if elsewhere).

```
build.cmd release    # rewritten functions replace the originals
build.cmd verify     # each rewritten function runs next to the original; differences are logged
```

## Run

```
build\verify\ei_launch.exe
```

Starts `F:\games\Evil Islands (Rebirth)\starter\Engine\game.exe` the way the Rebirth starter does
(working directory = game root, so the Rebirth addon still loads) and injects `ei_hook.dll`.
Use `--game <dir>` for another install. Nothing in the game folder is modified.
The log is written next to the DLL: `build\<config>\ei_hook.log`.

## Layout

- `src/game/` rewritten game code. Each function notes its original address.
- `src/hook/` the replacement framework (`include/ei/hook.h`).
- `src/render/` the new renderer (sokol_gfx on Direct3D 11, `third_party/sokol`), switched on with
  `EI_RENDER=new` (`docs/notes/renderer.md`).
- `tests/verify/` differential wrappers compiled into the `verify` build.
- `tools/` launcher, Ghidra export (`export_functions.py`), PE inspection (`pe.py`).
- `config/functions.csv` every function in game.exe with kind (game/lib/eh/thunk) and status.
- `docs/` cleanup plan, rewrite guide, ticket queue and per-area notes (`docs/notes/`).

### Vanilla (no Rebirth)

`launch-vanilla.cmd` runs the verify build against the plain GOG install in `F:\games\EvilIslands`. Functions that Rebirth patches in memory can be hooked there. On first use, `tools\setup_vanilla_run.cmd` builds `run\vanilla`: the GOG `game.exe` with Rebirth's dgVoodoo DLLs beside it. `run\` is not committed. The game's shared registry paths are redirected to the GOG folder for that process only.

### Automated harness

`harness.cmd <scenario>` runs a scenario on the vanilla install and exits with 0 when it passes. It
prints the report (`build\verify\harness_report.txt`): per-function verify counts, everything the
game console printed, and any failed checks. Scenarios live in `tests/scenarios/`; the steps are
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
  `clickregion/rclickregion/moveregion <n>` do the same at the centre of region `<n>` of the
  window on top (region numbers as the screen's code uses them; see `docs/notes/ui.md`).
  `clickunit <name> [l|r|move] [ctrl]` clicks the unit with that script name where the game's own
  picking finds it this frame (`ui_battle_commands.txt`).
- `screen <id>` asks the server for interface screen `<id>` as the global map's buttons do (4 the
  global map, 5 the camp); the camp's Esc returns to the game. `screen 5 <n>` opens the camp with
  constructor / trader class `<n>` as a base dialogue's "constrN" answer does (1, 3, 4, 5 after a
  new game; the first opening per class generates its trader stock, `trader_stock.txt`).
- `zone <name> [exit]` leaves the running zone as its exits do (the server's LeaveToZone for the
  local player) for map.txt zone `<name>` (`gz2g`, `bz1g`, ...), the party arriving at that zone's
  exit `<exit>` (from 0: map.txt's `#exit n` minus 1); `waitzone <seconds> <name>` waits until it is
  loaded. Starting a zone from the main menu crashes the game, so scenarios start with `newgame`
  and change zones from zone1 (`zone_gz2g_traps.txt`; zone list in `docs/notes/ui.md`, section 10).
- `speed <ms>` sets the real time per world update (game default 55, its own speed-up key 27);
  `speed 10` runs the game about 5.5x faster on real time. On the virtual clock a frame is five
  updates whatever their length, so it speeds nothing up: each frame only gets less virtual time,
  and what waits on sound or the clock (speech, script timers) takes more frames (SV1f). No
  scenario uses it since SV1i; a `wait <ms>` is 55 ms of game time per world update, so a scenario
  that needs the world to do something waits for it (`waitfor`) or waits long enough at 55 ms.
- `capture <name>` writes the next presented frame to `frames\<name>.bmp` in the run's folder;
  `freeze` / `unfreeze` hold the game's clocks and camera around captures; `samecapture <a> <b>`
  requires two captures to match. `python tools\compare_frames.py A B` compares two captures or two
  runs' frames with a tolerance (`--mask` restricts it to where two other frames differ).
  `render <spec>` turns the new renderer on for the run (its frame goes to `<name>.new.bmp`) and
  `oldterrain`, `oldobjects`, `oldinterface` (`off|on`) switch parts of the old draw
  (`docs/notes/renderer.md`, scenario `render_scene`).
- Harness runs use a virtual clock (`tests/verify/harness_clock.h`): each main-loop frame is five
  world updates, the main thread's sleeps and timed waits take no real time, sample ends come on
  virtual time, the date and the random seeds are fixed, and frames present without waiting for
  vsync. A scenario that repeats takes the same course on every run, so
  `python tools\compare.py <scenarios> --runs 3` can diff the console output of the original code and
  ours (those that do not repeat yet are listed in `docs/open-questions.md`). The harness's waits
  (`wait`, `waitfor`, `frames`, ...) count virtual time, each with a real-time safety limit. A
  `realtime` line puts a scenario on the real clock; `EI_HARNESS_CLOCK` (or `run_all.py` /
  `compare.py --clock`) overrides: `virtual` or `real`, plus `clocktrace` (every clock read and wait
  by function, caller and thread, in `ei_hook.log`) and `randtrace` (every random draw with its frame
  and caller, in `randtrace.txt`; `compare.py --clock virtual,randtrace` to find where two runs part).
  On the virtual clock the game's CRT heap also gives the same addresses every run, and units' update
  countdowns come from their ids on both sides (`tests/verify/harness_heap.h`); `lfheap` keeps the
  game's own heap and countdowns, `heaptrace` writes every call on the CRT heap with its callers to
  `heaptrace.txt` until world update `EI_HEAPTRACE` (default 40), then every object's address.
- The game-state digest (`tests/verify/harness_digest.h`): `digest <N>` (or `EI_DIGEST=<N>`) writes the
  world's state (units, positions, vitals, effects, AI state, generators, players) every N world updates to
  the run's `digest.txt`, `dump <name>` all of it once; `python tools\golden.py diff <run A> <run B>` names
  the first world update and field where two runs differ, and `golden.py record` / `compare` keep and check
  golden runs of the original code in `tests/golden/` (docs/notes/standalone-verification.md, SV2). The suite
  checks them with `python tools\work.py test <ID> --golden` (any run_all arguments): the games run with the
  goldens' digest and every run that has a golden is compared with it (SV3).
- Function vectors (`tests/vectors/vectors.h`): with `EI_VECTORS=1` the verify build runs the original of
  each function that has a recorder (`tests/vectors/record_<family>.cpp`) and writes a sample of its calls
  to the run's `vectors/` folder; `python tools\vectors.py merge <run folders>` keeps them in
  `tests/vectors/data`, and `ei_vectors.exe` (run by build.cmd) replays them against our code without the
  game (docs/notes/standalone-verification.md, SV4).
- Coverage (`tools/coverage.py`): with `EI_COVER=hooks` the hook build logs the first call of every
  hook and of its replacement; `coverage.py collect <run folders>` keeps which scenarios reach each one in
  `tests/coverage/reach.txt`, `coverage.py list` writes `tests/coverage/coverage.csv` (golden, vector,
  suite only or unreached, with a risk rank) and `coverage.py top` lists the largest gaps (SV5).

A scenario line `# prepare: <tool> [args]` runs `python tools\<tool> <harness_saves dir> [args]`
before the game starts (`tools/prepare_scenario.py`, called by harness.cmd and run_all.py): the
game sees those files under its `saves\` (e.g. `magic_traps.txt` and `tools/make_trap_mob.py`).

Timing runs: `EI_PLAIN=1` installs no verify wrappers, `EI_PROFILE=1` samples the main thread into the run's
`profile.txt` (`python tools\profile_report.py <run folder>`: time per function between harness steps), and
`EI_BUILD_DIR` points run_all at another verify build.
Hook installation (PF2) takes about 50 ms for the ~6000 hooks (it was ~200 ms: per-hook list scans, two VirtualProtect calls
per hook, one VirtualAlloc per trampoline); measure it as the time from the first `hooked`/`verifying` line to the first
`startup check` line in `ei_hook.log`. What remains is mostly the per-line log flush.

Add `--no-hooks` to run the same scenario on the original code only, as a baseline. Add
`--rebirth all` (or patch names, comma-separated) to turn on the Rebirth patch flags in the rewritten
code (`include/ei/rebirth.h`, `docs/notes/rebirth.md`); `ei_rebirth.ini` next to the DLL does the same
permanently, and a scenario can ask with `rebirthflags <names>`. A scenario's `resolution <WxH|desktop>`
and `widescreen 0|1` (or `EI_RESOLUTION` / `EI_WIDESCREEN`) answer the starter's settings the game and
Rebirth read for the run, without writing them (`tests/verify/harness_starter.h`; `python tools\run_all.py
--widescreen` compares Rebirth's WideScreen with ours, `docs/notes/rebirth.md`; `chat 1|0` opens and closes the
chat line, which a single player game does not show). Harness runs
need no clicks and leave the install alone: startup movies are skipped, the tutorial option is off,
saves and `config\tutorial.ini` go to `build\verify\harness_saves`, every other file the game writes under
its folder (the composed-skin cache `textures\*.mmp`, `config\keyboard.ini`, `config\addrbook.ini`,
`mp\*.mp`, ...) goes to a copy-on-write overlay there (reads prefer the overlay copy), settings are not written to the
registry, every game hosts and joins on a UDP port of its own instead of 8888 (`tests/verify/harness_net.h`: games run
side by side otherwise join each other's servers), Rebirth's starter switch CtfMon is answered off (addon.dll would otherwise close the user's ctfmon at start and
restart it from its DLL_PROCESS_DETACH, which hung every exit), and a game that hangs on exit is ended 3 s after
the scenario quits (where its threads were: `exit_hang.txt` in the run folder). When the game crashes,
the stack is in `ei_hook.log`; `python tools\resolve_crash.py` names the functions. A run that exceeds its time
limit logs where the main thread waits (instruction pointer and return addresses, as module+offset). No game outlives its limit:
the watchdog thread in the game ends it at the scenario's `timeout` (60 s unless the scenario says more); if that
thread cannot run, `ei_launch` ends the game 20 s later (and the game runs in a job object that dies with
`ei_launch`, children included). `run_all` reports both as `TIMEOUT`; `ei_launch` exits 4 (or 5 when Windows could not
end the process: a thread stuck in a driver call). `tests\scenarios\watchdog_*.txt` check this (`# expect: TIMEOUT`).

    harness.cmd tests\scenarios\sandbox_actions.txt

`python tools\run_all.py` runs every scenario on both installs, several games at once (`--jobs N`,
default 16; the whole suite takes about 2 minutes). Each run writes to its own folder,
`build\verify\runs\<scenario>-<install>` (`ei_launch --out`, `EI_OUT_DIR`): its `ei_hook.log`,
report and `harness_saves`, emptied first, so every run starts without saves or texture cache.
For parallel runs the harness ignores other game windows (the game otherwise exits when it finds
one), keeps the game active when it loses focus (it otherwise pauses), and opens data files for
shared reading. Runs of our code draw with our renderer (`EI_RENDER=new,present`, no display mode
change, so none needs the machine to itself); dgVoodoo draws the original code (`--no-hooks`,
`--widescreen`'s reference, compare.py's original side), scenarios whose `render` step leaves out
`present`, and everything with `--dgvoodoo` or `--widescreen` (its frames are compared with the original's). Passed runs of the original code are cached in
`build\reference` by scenario commit and install (`--fresh` plays them again); docs/notes/renderer.md, R10.
`--standalone` runs `ei_standalone.exe` instead of game.exe (install `standalone`: run\vanilla's game.exe laid
out without its code, the vanilla data, our renderer; `ei_launch --standalone`), by default the `boot` scenario;
an address with no rewrite yet ends the run with `standalone: no code at ...` (docs/notes/standalone.md).
A game that Windows gave the foreground and that lost it to a starting game is
minimised by dgVoodoo, which can take its surfaces for good; a dgVoodoo run that fails after that is run again. `python tools\resolve_crash.py build\verify\runs\<run>` reads a run's crash.
