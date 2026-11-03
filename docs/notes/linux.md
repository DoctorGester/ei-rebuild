# Linux

The game builds on Linux (x86-64, gcc 13 or clang 18) from the same CMakeLists.txt as on Windows: the game and
library code unchanged, SDL3 for the window, input, sound and threads, POSIX for the rest of the platform
(src/platform/posix/platform_posix.cpp), and the renderer on SDL_GPU's Vulkan driver (src/render/gpu.cpp;
docs/notes/renderer.md section 15c). The harness builds too (`-DEI_HARNESS=ON`, "The
harness" below); the offline checks against Windows' drawing and keys stay Windows' only.

## Build

Packages (Ubuntu 24.04; SDL3 finds the rest itself, Wayland's too when its development packages are there):

```sh
sudo apt-get install cmake ninja-build libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxss-dev \
    libxfixes-dev libxtst-dev libxkbcommon-dev libgl-dev libegl-dev libasound2-dev libpulse-dev     python3 mesa-vulkan-drivers
```

The configure step also fetches and builds the HLSL toolchain, SDL_shadercross with DXC's Linux release, once
(docs/notes/renderer.md, "HLSL and SDL_shadercross"). `mesa-vulkan-drivers` brings lavapipe and the Vulkan loader for
the game without a GPU and for the checks.

Then:

```sh
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/linux
```

The build has no gcc warnings, plain and with `-DEI_HARNESS=ON`; the Linux CI job configures with
`-DCMAKE_COMPILE_WARNING_AS_ERROR=ON`, so a new warning fails it (the Windows builds are not affected). Check in WSL
from a clone in the WSL home, not under /mnt/e (9P is slow).

## Checks

`ctest --test-dir build/linux` (on a machine without a display: `xvfb-run -a ctest`, Mesa's lavapipe draws):

- `port_platform` (tests/port/port_check.cpp): path folding, files and mappings, .ini reads and writes, the
  one-instance lock, UDP, the settings document's checks (tests/unit/settings_file.cpp).
- `port_render` (tests/port/render_check.cpp): the interface canvases' glyph cases on Vulkan against their CPU
  reference (tests/render/glyph_cases.cpp, which tools/ui_check runs on Direct3D 12), and the window's present after
  resizes: a frame of known rows presented through the swap chain at four sizes, the window read back from the X
  server (XGetImage).
- `port_shaders` (tools/shader_check): every HLSL program and its pipelines on SDL_GPU's Vulkan driver (lavapipe
  without a GPU), with `--require`: no Vulkan device fails.
- `port_gpu` (tools/gpu_check): the renderer's SDL_GPU layer (src/render/gpu.cpp; renderer.md section 15c) on the
  same driver: textures, buffers, draws in order, downloads, the upload ring, the swap chain on a small window of the
  virtual display; `--require` as above.

The Linux workflow (.github/workflows/linux.yml) runs them on GitHub's Ubuntu runner for every push.

## Run

`build/linux/ei-rebuild` plays the game folder named by `EI_GAME_ROOT`, else the one it sits in; `EI_SETTINGS_PATH`
pins the settings file. The folder's `Work path` and `CD-ROM Path` are read as the game folder itself. Game data
names are matched as Windows matches them: `res\database.res` opens `Res/Database.RES` (one folder at a time; the
exact spelling first). Arguments for the game follow `--`. Under WSLg Mesa draws on llvmpipe (the CPU) unless
`GALLIUM_DRIVER=d3d12` puts it on the Windows GPU.

## Release

The Release workflow (.github/workflows/release.yml, run by hand) builds Linux beside Windows and puts
`ei-rebuild-linux-x64.tar.gz` (`ei-rebuild-linux-x64/`: `ei-rebuild`, `README.txt`, `third_party_notices.txt`) on the
same GitHub release. Its `linux` job runs in an `ubuntu:22.04` container so that the binary needs no newer glibc than
22.04's 2.35: gcc 13 from the ubuntu-toolchain-r PPA (22.04's gcc 11 has no `<format>`) with `-static-libstdc++
-static-libgcc`, cmake from pip (CMakeLists.txt wants 3.24, 22.04 has 3.22), `-DCMAKE_BUILD_TYPE=Release`, the port
checks under `xvfb-run`, then `strip`. The Package step fails on any `GLIBCXX_` import, writes the newest `GLIBC_`
version the binary imports into the run's summary and the README, and lists its libraries: `libGL.so.1`, `libm`,
`libc` (SDL3 is static and opens X11, sound and the rest itself).

glibc: built on 24.04 the same steps need 2.38 (`__isoc23_*` and `strl*` from 2.38's headers, `arc4random` 2.36,
which SDL finds at configure time); on 22.04's headers those are absent, and what remains tops out at 2.35
(`_dl_find_object` from the static libgcc unwinder, `hypotf`), so the oldest systems are those with glibc 2.35:
Ubuntu 22.04, Debian 12, Fedora 36, not RHEL 9 (2.34). The container run itself has not been tried (no Docker on the
build machine); the first release's summary line is the measured answer.

## The harness

`cmake -S . -B <build> -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEI_HARNESS=ON` builds `ei-rebuild` with the harness
(tests/verify: its platform backend and its own system layer's system_posix.cpp; harness.md, "The harness's code"). The
Linux workflow builds it. Scenarios run with run_all.py as on Windows (`python3 tools/run_all.py ...`), with
`EI_BUILD_DIR` the build folder, `EI_GAME_VANILLA` / `EI_GAME_REBIRTH` the game folders (copies: a run never writes
into them, but WSL reads /mnt slowly), and `EI_HARNESS_SETTINGS=<checkout>/config/harness` when the build folder is not
two levels below the checkout. run_all's machine slots are the system's own: games in WSL do not count against
Windows' 16, so hold slots on the Windows side while they run. Under WSL keep the checkout and the build in WSL's home
(gcc over /mnt runs at a quarter of the speed).

First runs (WSLg, the sokol OpenGL renderer on llvmpipe, Mesa's software OpenGL; 2026-11-03), the image suite against the Windows goldens: 141 of
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

Harness runs open no sound device; `EI_HARNESS_SOUND=1` keeps the real one (sound.md, "How the sound keeps up").

### Sound under WSLg

WSLg's only output is pulseaudio's "RDP Sink" (`PULSE_SERVER=unix:/mnt/wslg/PulseServer`), and on the build machine
(2026-11-03) it plays slower than real time: the sink takes 44100 frames a second while the latency it reports grows
by 80 to 90 ms each second (44.1 or 48 kHz, writes of 768 to 4096 frames alike), so the sound lags further and
further; at 1.1 to 1.7 s of backlog, 13 to 18 s in, it nearly stops taking anything (768 frames every 1 to 3 s), and a
game started a little later finds it stalled from the start. A plain libpulse writer (pa_simple, no SDL, no game)
does the same, so it is the WSLg / RDP path, not ours; nothing in the game can make the sink play. What the game
does see to: its thread never waits for the device (sound.md), so a stalled sink leaves the game silent but running at
its frame rate (before, the first jingle after the stall froze the main loop until the 60 s limit). Frame rate and
sound are unrelated there: the menu's passes fall from 100 to about 30 a second over a minute with no device too.

## What differs from Windows

- No lost context is looked for (it needs a robust context).
- The keys: SDL's scancodes through sdl_keys.cpp's US rules; no layout's own key codes or control characters
  (`LayoutKey` / `LayoutControlCharacter`), and the key names are SDL's English ones.
- Not played with real data yet: menus, maps, movies, sound, saves and multiplayer are unchecked on Linux.
- Frame pacing under WSLg (X11 through XWayland, GALLIUM_DRIVER=d3d12, VSync on; PL20, 2026-11-03): 64 to 85 frames a
  second, 10 to 28 ms apart, where Windows' 60 Hz screen gives 16 / 17 ms; on a busy machine 32 to 38, 16 to 66 ms
  apart. XTest pointer moves at 125 Hz (4 counts) arrive as 4, 8 or 12 counts a frame at 70 a second and 8 to 40 at 35:
  the beat between the mouse's reports and the frames, and the frame's length. The camera's mouse turn took each frame's
  motion as its rate, so it ran with both; it now takes the mouse's speed over a 60 Hz frame's window (ui.md, "Camera
  mouse turn"): the same moves give a drag of 5 to 14 (8.3 is the speed), 1000 Hz moves of 1 count 16 to 22 (16.7). The SDL build in WSL has no Wayland backend (no
  wayland-client development files), so SDL_VIDEO_DRIVER=wayland was not measured.
