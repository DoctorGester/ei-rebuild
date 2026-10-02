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
- `tests/verify/` differential wrappers compiled into the `verify` build.
- `tools/` launcher, Ghidra export (`export_functions.py`), PE inspection (`pe.py`).
- `config/functions.csv` every function in game.exe with kind (game/lib/eh/thunk) and status.
- `docs/` survey and notes.

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
- `speed <ms>` sets the real time per world update (game default 55, its own speed-up key 27);
  `speed 10` runs the game about 5.5x faster.

Add `--no-hooks` to run the same scenario on the original code only, as a baseline. Harness runs
need no clicks and leave the install alone: startup movies are skipped, the tutorial option is off,
saves and `config\tutorial.ini` go to `build\verify\harness_saves`, settings are not written to the
registry, and a game that hangs on exit (Rebirth) is ended 10 s after the scenario quits. When the game crashes,
the stack is in `ei_hook.log`; `python tools\resolve_crash.py` names the functions.

    harness.cmd tests\scenarios\sandbox_actions.txt
