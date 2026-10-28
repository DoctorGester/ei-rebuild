# Evil Islands (Проклятые Земли) rebuild

Readable C++ source for the Russian `game.exe` 1.06 of Evil Islands, rebuilt with modern MSVC (x86). The goal is
identical game behaviour, not identical bytes: the same world, saves and network play as the original, with a new
renderer and the Rebirth edition's fixes on top.

The product is the standalone game: `ei_standalone.exe` loads `ei_hook.dll`, which holds all of the game's code. It
reads the game's data from an existing install and needs no `game.exe`. The original `game.exe` is used only by the
test harness, as the reference our code is compared with.

## Status

| Area | State | Notes |
|---|---|---|
| Game code | Done | 6,032 of 6,177 game functions rewritten (99.5% of game code by size); the rest is dead or library code |
| Standalone game | Done | Runs without `game.exe`; the build reads no `game.exe` either |
| Behaviour vs the original | Checked | 145 harness scenarios; 121 with goldens match the original's world state and saves exactly |
| Saves | Done | Same format as the original; decoded and compared field by field in the tests |
| Multiplayer | Works | Hosting and joining over UDP; tested with two of our games in lockstep |
| Rebirth edition fixes | Done | Rebirth's bug fixes and options built in, each behind a flag (`docs/notes/rebirth.md`) |
| Widescreen | Done | Rebirth's widescreen layout in our own code |
| Renderer | Done, improving | Direct3D 11 (sokol_gfx); per-pixel lighting on the GPU, soft sun and moon shadow maps, smooth camera, vsync option, performance overlay. The old Direct3D 7 / DirectDraw layer is gone (R27), a renderer that fails to start stops the game, and skins, wounds and item pictures are composed on the GPU (R28; 2DintMMX.dll no longer loaded). Next: the remaining CPU-built textures (R28a-c), point-light shadows, water, ambient occlusion |
| UTF-8 | Done | Every string inside the game in UTF-8; saves and the network stay cp1251, as the original's |
| Native interface | In progress | Interface drawn by the new renderer directly (text atlas, menus, HUD): surfaces and GPU-built textures done (P1, P7), the rest in tickets P2-P8 |
| Code cleanup | Style phase | Porting artifacts done: real C++ classes (C28), no MFC or STLport left (C14), no game addresses or registrations (C20), the custom CRT cut down to what behaviour needs (C35). Now readability: comments, literals, raw byte copies, shared enums and constants (C41-C46 done); next small mechanical cleanups (C47), `enum class` everywhere and names for the remaining magic numbers (C45c, C45d) |
| Lua mission scripts | Planned | `docs/notes/lua-scripting.md` |

The work queue is `docs/tickets.md` (generated from `docs/tickets.toml` by `tools/tickets.py`).

## Requirements

- Windows 10 or later.
- The VS 2022 Build Tools with MSVC 14.43 for x86, and CMake and Ninja from the same install. `build.cmd` looks in
  `E:\bin\vs\buildtools`; set `EI_VSROOT` to your Build Tools folder if it is elsewhere.
- Python 3 (the build runs checks written in Python).
- An installed copy of the game for its data:
  - the GOG release (`EI_GAME_VANILLA`, default `F:\games\EvilIslands`), and/or
  - the Rebirth edition (`EI_GAME_REBIRTH`, default `F:\games\Evil Islands (Rebirth)`).

  Nothing in the game folder is ever written: saves, settings and caches go to the play folder (`run\play`).

## Build

```
build.cmd release        the game: build\release\ei_standalone.exe, ei_hook.dll, ei_launch.exe
build.cmd verify         the game plus the test harness, into build\verify (what the tests run)
```

The build also runs the source checks (`tools/check_*.py`, the generated files' freshness); none reads `game.exe`.

## Play

```
play.cmd            builds the UTF-8 release and plays it on the Rebirth install's data, every option on
play.cmd vanilla    the same on the GOG install's data, with Rebirth's bug fixes and widescreen
launch-vanilla.cmd  plays the last release build on the GOG install's data, default options
```

For example, with the game somewhere else:

```
set EI_GAME_REBIRTH=D:\Games\Evil Islands
play.cmd
```

The game plays from a copy of the build in `run\play`, so a game left open never locks the build. Its settings are
`run\play\ei_settings.ini` (imported once from the registry the first time). The performance overlay and vsync are
switched there (comments at the top of `play.cmd`; `docs/notes/renderer.md` section 15). The log is
`run\play\ei_hook.log`.

## Test

```
harness.cmd tests\scenarios\smoke.txt     one scenario on our game, exit code 0 when it passes
python tools\run_all.py --golden          every scenario, 16 games at once, compared with the goldens
python tools\run_all.py --no-hooks        the same scenarios on the original game.exe
```

Scenarios are text files in `tests/scenarios/`. The harness runs the game hidden on a virtual clock, so a scenario
repeats exactly, and compares the world state with goldens recorded from the original (`tests/golden/`). The
original's runs are cached and play again only when the harness changes. How to write scenarios and what a run does:
`docs/notes/harness.md`; how changes are checked: `docs/notes/standalone-verification.md`.

The test tools still expect the installs at the default `F:\games` paths (`tools/run_all.py`), and need
`tools\setup_vanilla_run.cmd` once for the original's runs.

## Layout

- `src/game/` the game code. Each function names the original function it replaces.
- `src/lib/` small shared pieces: the CRT parts behaviour depends on (formatting, x87 maths), file and text helpers, byte copies.
- `src/render/` the renderer (sokol_gfx on Direct3D 11, `third_party/sokol`; `docs/notes/renderer.md`).
- `src/standalone/` the host exe and the generated parts of the image.
- `src/hook/` the DLL's start-up inside the standalone exe, logging, and the Rebirth options.
- `tests/verify/` the harness; `tests/vectors/` function-level comparisons with the original; `tests/scenarios/`.
- `tools/` the launcher, build checks, test runner, ticket and progress tools.
- `docs/` how to work on the code (`worker-brief.md`, `rewrite-guide.md`, `cleanup-plan.md`), the tickets, and one
  note per area in `docs/notes/` (index: `docs/notes/README.md`).

## License

The original work in this repository (the tools, the test harness, the renderer and the rebuilt source as written
here) is under the MIT license (`LICENSE`). Libraries in `third_party/` keep their own licenses.

Evil Islands (Проклятые Земли) and its data are the property of their rights holders (Nival). This repository holds no
game files: no `game.exe`, no resources, no music or video. To build or play it you need your own copy of the game.
The Rebirth edition's fixes are reimplemented from its behaviour; the Rebirth edition belongs to its authors.
