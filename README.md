# Evil Islands (Проклятые Земли) rebuild

Readable C++ source for the Russian `game.exe` 1.06 of Evil Islands, rebuilt with modern MSVC (x86 and x64). The goal is
identical game behaviour, not identical bytes: the same world, saves and network play as the original, with a new
renderer and the Rebirth edition's fixes on top.

The product is the standalone game: `ei-rebuild.exe`, one exe that holds all of the game's code. Dropped into an
installed game's folder (GOG or Rebirth) it plays from there, with no other file of ours; it needs no `game.exe`. The original `game.exe` is used only by the
test harness, as the reference our code is compared with.

## Status

The whole game runs on the rebuilt code and is playable in single player and over the network. Every scenario in the
test suite plays out exactly as on the original: the same world state and the same save files.

## What you get

- **One exe, no installer.** Drop `ei-rebuild.exe` into a GOG or Rebirth install and start it. It needs no
  `game.exe`, no `binkw32.dll`, no `mss32.dll` and no registry entries.
- **Your saves and multiplayer still work.** Saves are in the original's format, so your old saves load. Hosting and joining
  network games work as before.
- **A 64-bit build** next to the classic 32-bit one, with the same behaviour.
- **A new renderer** on Direct3D 11: per-pixel lighting, soft sun and moon shadows, MSAA and anisotropic filtering,
  a smooth camera, vsync, and an optional performance overlay.
- **Widescreen** at any resolution, with the interface scaled to the window.
- **The Rebirth edition's fixes built in**, including its bug fixes and options, on both the GOG and Rebirth data.
- **A new interface layer** drawn by the renderer, with sharp text at any size.
- **New sound and video code:** sound plays through miniaudio, caves get their reverb back, and the game's Bink movies
  play through its own decoder.
- **Unicode throughout.** Names and text are UTF-8 inside the game; saves and the network stay in the original's
  encoding.

## Differences from the original

- Settings live in `ei_settings.ini` beside the exe, a plain ini (`[video]`, `FullScreen = true`), not in the
  registry; one file serves both installs.
- Graphics differ: the lighting, shadows and texture filtering are new, so screenshots won't match the original's.
- Sound volume, panning and cave reverb follow our own curves, since the original's sound library is not used.
- Some MP3 voice lines start up to about 90 ms earlier than with the original's decoder.
- Rebirth's bug fixes and widescreen are on by default, even on GOG data; `ei_rebirth.ini` turns them off.

The source is readable modern C++: every function is rewritten from the original, with no leftover MFC or STLport.
Lua mission scripting is planned (`docs/notes/lua-scripting.md`). The work queue is `docs/tickets.md`.

## Requirements

- Windows 10 or later.
- The VS 2022 Build Tools with MSVC 14.43 for x86 and x64, and CMake and Ninja from the same install. `build.cmd` looks in
  `E:\bin\vs\buildtools`; set `EI_VSROOT` to your Build Tools folder if it is elsewhere.
- Python 3 (the build runs checks written in Python).
- An installed copy of the game for its data:
  - the GOG release (`EI_GAME_VANILLA`, default `F:\games\EvilIslands`), and/or
  - the Rebirth edition (`EI_GAME_REBIRTH`, default `F:\games\Evil Islands (Rebirth)`).

  Nothing in the game folder is ever written: saves, settings and caches go to the play folder (`run\play`).

## Build

```
build.cmd release        the game: build\release\ei-rebuild.exe (and ei_launch.exe, the tools' launcher)
build.cmd verify         the game with the test harness linked in, and ei_harness.dll (the harness for the original
                         game.exe), into build\verify (what the tests run)
```

The build also runs the source checks (`tools/check_*.py`, the generated files' freshness); none reads `game.exe`.

## Play

```
play.cmd            builds the UTF-8 release and plays it on the Rebirth install's data, every option on
play.cmd vanilla    the same on the GOG install's data, with Rebirth's bug fixes and widescreen
play.cmd x64        the 64-bit build (build\play-x64, run\play-x64) on the same data; `play.cmd vanilla x64` too
launch-vanilla.cmd  plays the last release build on the GOG install's data, default options
```

Or copy `build\release\ei-rebuild.exe` into the game's folder and start it there: it writes `ei_settings.ini`
beside itself with the defaults and picks the Rebirth flags from the folder (Rebirth's profile where Rebirth's plugin
is, else the bug fixes and widescreen; an `ei_rebirth.ini` beside it overrides that). It writes into that folder
(settings, saves, `ei-rebuild.log`), so use a copy of the install if you want the original kept as it is.

For example, with the game somewhere else:

```
set EI_GAME_REBIRTH=D:\Games\Evil Islands
play.cmd
```

The game plays from a copy of the build in `run\play`, so a game left open never locks the build. Its settings are
`run\play\ei_settings.ini` (written with the defaults when missing: full screen at the desktop's size, every graphics
setting at its best; the registry is never read). The render quality (MSAA samples,
anisotropy), the performance overlay and vsync are set in its `[renderer]` section (comments at the top of `play.cmd`;
`docs/notes/renderer.md` sections 11 and 15). The log is
`run\play\ei-rebuild.log`.
`play.cmd x64` does the same in `run\play-x64` (its own settings and log) with the 64-bit build.

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
- `src/lib/` small shared pieces: the CRT parts behaviour depends on (formatting, the game CRT's pow / acos / asin / hypot), file and text helpers, byte copies.
- `src/render/` the renderer (sokol_gfx on Direct3D 11, `third_party/sokol`; `docs/notes/renderer.md`).
- `src/standalone/` the exe's start-up and the generated parts of the image.
- `src/hook/` logging and the Rebirth options (both homes), and the harness's game.exe side (ei_harness.dll).
- `tests/verify/` the harness; `tests/vectors/` function-level comparisons with the original; `tests/scenarios/`.
- `tools/` the launcher, build checks, test runner, ticket and progress tools.
- `docs/` how to work on the code (`worker-brief.md`, `rewrite-guide.md`, `cleanup-plan.md`), the tickets, and one
  note per area in `docs/notes/` (index: `docs/notes/README.md`).

## License

The original work in this repository (the tools, the test harness, the renderer and the rebuilt source as written
here) is under the MIT license (`LICENSE`). Libraries in `third_party/` keep their own licenses. The interface fonts
(`third_party/fonts`, Liberation, SIL Open Font License 1.1) are built into the executable; every build writes
`third_party_notices.txt` beside it with the third-party licences, and `play.cmd` copies it along.

Evil Islands (Проклятые Земли) and its data are the property of their rights holders (Nival). This repository holds no
game files: no `game.exe`, no resources, no music or video. To build or play it you need your own copy of the game.
The Rebirth edition's fixes are reimplemented from its behaviour; the Rebirth edition belongs to its authors.
