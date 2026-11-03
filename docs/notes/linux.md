# Linux

The game builds on Linux (x86-64, gcc 13 or clang 18) from the same CMakeLists.txt as on Windows: the game and
library code unchanged, SDL3 for the window, input, sound and threads, POSIX for the rest of the platform
(src/platform/posix/platform_posix.cpp), and the renderer on sokol_gfx's OpenGL 4.5 backend
(src/platform/gl/graphics_gl.cpp; docs/notes/platform.md, "Graphics"). The harness builds too (`-DEI_HARNESS=ON`, "The
harness" below); the offline checks against Windows' drawing and keys stay Windows' only.

## Build

Packages (Ubuntu 24.04; SDL3 finds the rest itself, Wayland's too when its development packages are there):

```sh
sudo apt-get install cmake ninja-build libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxss-dev \
    libxfixes-dev libxtst-dev libxkbcommon-dev libgl-dev libegl-dev libasound2-dev libpulse-dev
```

sokol-shdc, the shader compiler (CMakeLists.txt looks in build/tools, then PATH, or takes -DEI_SHDC):

```sh
mkdir -p build/tools
curl -L https://raw.githubusercontent.com/floooh/sokol-tools-bin/b975d5642093d62e76a1e088bdae945d6d8b8ff2/bin/linux/sokol-shdc -o build/tools/sokol-shdc
chmod +x build/tools/sokol-shdc
```

Then:

```sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/linux
```

## Checks

`ctest --test-dir build/linux` (on a machine without a display: `xvfb-run -a ctest`, Mesa's llvmpipe draws):

- `port_platform` (tests/port/port_check.cpp): path folding, files and mappings, .ini reads and writes, the
  one-instance lock, UDP, the settings document's checks (tests/unit/settings_file.cpp).
- `port_render` (tests/port/render_check.cpp): the interface canvases' glyph cases on OpenGL against their CPU
  reference (tests/render/glyph_cases.cpp, which tools/ui_check runs on Direct3D 11).

The Linux workflow (.github/workflows/linux.yml) runs both on GitHub's Ubuntu runner for every push.

## Run

`build/linux/ei-rebuild` plays the game folder named by `EI_GAME_ROOT`, else the one it sits in; `EI_SETTINGS_PATH`
pins the settings file. The folder's `Work path` and `CD-ROM Path` are read as the game folder itself. Game data
names are matched as Windows matches them: `res\database.res` opens `Res/Database.RES` (one folder at a time; the
exact spelling first). Arguments for the game follow `--`.

## The harness

`cmake -S . -B <build> -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEI_HARNESS=ON` builds `ei-rebuild` with the harness
(tests/verify: its platform backend and its own system layer's system_posix.cpp; harness.md, "The harness's code"). The
Linux workflow builds it. Scenarios run with run_all.py as on Windows (`python3 tools/run_all.py ...`), with
`EI_BUILD_DIR` the build folder, `EI_GAME_VANILLA` / `EI_GAME_REBIRTH` the game folders (copies: a run never writes
into them, but WSL reads /mnt slowly), and `EI_HARNESS_SETTINGS=<checkout>/config/harness` when the build folder is not
two levels below the checkout. run_all's machine slots are the system's own: games in WSL do not count against
Windows' 16, so hold slots on the Windows side while they run. Under WSL keep the checkout and the build in WSL's home
(gcc over /mnt runs at a quarter of the speed).

First runs (WSLg, llvmpipe: Mesa's software OpenGL; 2026-11-03), the image suite against the Windows goldens: 141 of
149 the same in the full run, and ui_modifier_keys and ui_saveload_delete too once the folder listing and the key line
were fixed (a run of the rest); the Rebirth set without its pairs 22 of 23. What still differs, and why:

- The pair scenarios (net_pair_leave, net_pair_speed, net_pair_zone; net_pair_master's peer lists the server one poll
  sooner): llvmpipe draws a frame step in about 140 ms (6 ms on Windows' GPU), so a pair's run passes its frame waits
  and its time limit (about 190 s against Windows' 30 to 50 s) and the turns fall differently. Not run again: they
  exceed the 60 s game limit there.
- net_second_master (both installs): a TCP connect to a closed loopback port is refused at once on Linux; Windows tries
  again for about a second, so the scenario's "the box is up, Esc cancels it" finds the box already gone.
- render_present_window: `windowsize` resizes asynchronously under X11 / WSLg (SDL_SetWindowSize), so the clicks after it
  aim at a size the window does not have yet.
- Frames, not digests: render_smooth and ui_quicksave's saved picture against the stored one (94% of the pixels
  differ: the read-back's orientation or format), render_terrain_seams' level steps on the vanilla data (0.9% against
  at most 0.3%).

Harness runs open no sound device, so the audio lag seen when playing under WSLg (PL17a) does not show in them.

## What differs from Windows

- No lost context is looked for (it needs a robust context).
- The keys: SDL's scancodes through sdl_keys.cpp's US rules; no layout's own key codes or control characters
  (`LayoutKey` / `LayoutControlCharacter`), and the key names are SDL's English ones.
- Not played with real data yet: menus, maps, movies, sound, saves and multiplayer are unchecked on Linux.
