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
    libxfixes-dev libxtst-dev libxkbcommon-dev libgl-dev libegl-dev libasound2-dev libpulse-dev     python3 mesa-vulkan-drivers
```

The configure step also fetches and builds the HLSL toolchain, SDL_shadercross with DXC's Linux release, once
(docs/notes/renderer.md, "HLSL and SDL_shadercross"). `mesa-vulkan-drivers` brings lavapipe and the Vulkan loader for
`port_shaders`.

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

The build has no gcc warnings, plain and with `-DEI_HARNESS=ON`; the Linux CI job configures with
`-DCMAKE_COMPILE_WARNING_AS_ERROR=ON`, so a new warning fails it (the Windows builds are not affected). Check in WSL
from a clone in the WSL home, not under /mnt/e (9P is slow).

## Checks

`ctest --test-dir build/linux` (on a machine without a display: `xvfb-run -a ctest`, Mesa's llvmpipe draws):

- `port_platform` (tests/port/port_check.cpp): path folding, files and mappings, .ini reads and writes, the
  one-instance lock, UDP, the settings document's checks (tests/unit/settings_file.cpp).
- `port_render` (tests/port/render_check.cpp): the interface canvases' glyph cases on OpenGL against their CPU
  reference (tests/render/glyph_cases.cpp, which tools/ui_check runs on Direct3D 11), and the window's present after
  resizes: a frame of known rows presented at four sizes, the window read back from the X server (XGetImage; a GL
  front-buffer read gives zeros on Mesa's software paths).
- `port_shaders` (tools/shader_check): every HLSL program and its pipelines on SDL_GPU's Vulkan driver (lavapipe
  without a GPU), with `--require`: no Vulkan device fails.
- `port_gpu` (tools/gpu_check): the renderer's SDL_GPU layer (src/render/gpu.cpp; renderer.md section 15c) on the
  same driver: textures, buffers, draws in order, downloads, the upload ring, the swap chain on a small window of the
  virtual display; `--require` as above.

The Linux workflow (.github/workflows/linux.yml) runs them on GitHub's Ubuntu runner for every push.

## Run

`build/linux/ei-rebuild` plays the game folder named by `EI_GAME_ROOT`, else the one it sits in; `EI_SETTINGS_PATH`
pins the settings file. The folder's `Work path` is read as the game folder itself. Game data
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
two levels below the checkout. run_all's machine slots are the system's own (lock files in its temp folder, Linux's
/tmp under WSL): games in WSL do not count against Windows' 16, so under WSL run_all allows 6 at once by default (WSL's
memory is capped at 16 GB; `EI_HARNESS_SLOTS` overrides it), and a local Linux suite runs only while no Windows suite
does (hold slots on the Windows side for a few WSL games beside Windows work). Under WSL keep the checkout and the build in WSL's home
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

## The stub data

The suites can run without the game's files: `tools/stub_data.py build <install> <folder>` writes a copy of an install
with every picture, sound and movie blanked in its own format, so the game reads it through its ordinary loaders, and
the goldens stay those of the install. The repository is public: the stub set is still the game's data (maps, models,
scripts, texts) and lives only under `build/` and in a private repository, never in git here, a public release or an
Actions cache.

What the runs read (`EI_READTRACE=1`, summed by `stub_data.py reads`; both suites, 2026-11-03): from Res\ every
archive the game opens at start (aiinfo, database, databaselmp, figures, gfx, materials, menus, outro, redress, sfx,
speech, texts, textslmp, textures) and music.reg, smessbase.reg, streamsn.reg, sysmsg.txt; the maps the scenarios play
(.mpr, .mob, every .mq); camera\mainmenu.cam and a zone's .cam; config\ai.reg and the lights .ini files; the loading
screen's, the titles' and the credits' movies (progres*.bik, titles.bik, crdtfin.bik); the music the zones choose
(stream\*.mp3); on Rebirth also EI_Plugin.ini, 2utoexec.cfg and starter\mods\standard\config.reg. Looked for and
absent: autoexec.cfg, the loose textures\*.bmp overrides, the mod folder's copies, and starter\addon.dll /
EI_Plugin.asi (whose presence picks the Rebirth profile). Never read: the install's saves\ (a run's are its own
folder's), its programs, the starter's own files.

What of them reaches game state, and so what the stubs keep:
- Kept whole: maps (terrain, sectors, objects, scripts, quests), figures (geometry, skeletons, animations and their
  events), databases, texts, registry files, cameras, configs, fonts, the Rebirth mods.
- Textures (.mmp, in any archive and inside nested ones: .mpr, .mod are archives too) and loose .bmp: the header (size,
  format, levels) and the .mmp's extra block; the pixels 0.
- Sounds (.wav): every chunk; the samples 0. A voice ends by its data's bytes over its byte rate (sound.md).
- MP3 (speech, music): every frame header (the game counts frames: ScanMp3), the frames' data 0, an info frame whole.
- Movies (.bik): header and frame index (frame count, size, rate, keyframes); every frame emptied (no sound packet, no
  video packet: the decoder keeps the frame before). The game reads a movie's frame numbers, never its pixels.
- Programs (.exe, .dll, .asi): empty files, for the profile's presence check.

Checked (2026-11-03, the read-traced install runs against the stub runs): on Windows the image suite on the stub set
149 the same / 5 without a golden and the Rebirth suite 28 the same, as on the installs. Four scenarios fail their own
pixel checks on blank textures, their goldens the same: render_blink, render_cursor_present, render_shadows_outside,
render_smooth. `stub_data.py check <stub> <install> <runs>` confirms every file the runs read is in the stub set at
the install's size (the movies aside). In WSL (the packed set unpacked as the CI job does)
each run on the stub set against the same run on the install copies, digest for digest (golden.py diff): the image
suite less the pairs 142 of 142 comparable runs the same (4 of them only once rerun on a quiet machine: under load
llvmpipe's frame waits run out), camera_smooth and camera_spin cut by their 120 s limit on both, the watchdog
scenarios without a digest; the Rebirth set less the pairs 22 of 22. Against the Windows goldens every Linux run
differs from record 0, on the install copies as on the stubs (H47c), so the CI job fails on every golden until H47c is
fixed. Over 60 s on llvmpipe besides the pairs: camera_smooth, camera_spin, render_stats, zone_gz15h_tornado.

Sizes: the vanilla set 724 MB on disk (121 MB of it not zero: figures.res 53, the maps 59), Rebirth's 1850 MB (240 MB
not zero): its high-resolution textures.res is 655 MB of blanked pixels and the LostInAstral mod 711 MB (its speech and
sfx blanked; 119 MB of maps and figures that no run reads). The blanks cost next to nothing packed; truncating them
would change the archives' layout and only save the runner's disk. `stub_data.py pack` (Python 3.14's zstd) puts both
in one .tar.zst of about 75 MB: level 22 with a 128 MB window and long matching, the two installs' copies of a file next
to each other. Against xz -9 with a 256 MB window: 65.0 MB, but 7 to 9 s to unpack against zstd's 1 s; zstd -19
--long=27 76.3 MB. Leaving out LostInAstral would give 49 MB. Building takes about 20 s an install, packing 5 minutes.

### The CI job

`.github/workflows/suites-linux.yml` ("Suites (Linux)") builds the harness on GitHub's Ubuntu runner, checks out the
private repository with a read-only token, unpacks the stub set into the runner's temp folder, removes the checkout
and plays both suites under xvfb on llvmpipe, less `LINUX_SKIP` (the scenarios that do not match on llvmpipe yet, "The
harness" above); it fails when a golden differs. It runs on pushes to main and `claude/**`, on pull requests from this
repository's own branches (a fork's gets no secrets and the job is skipped) and by hand; it uploads nothing, and the
data is outside every cached path. It is off until the variable below is set.

One-time setup (the user's; nothing here creates repositories or tokens):
1. Build and pack on the machine with the installs:
   `python tools/stub_data.py build F:\games\EvilIslands build\stub\vanilla`,
   `python tools/stub_data.py build "F:\games\Evil Islands (Rebirth)" build\stub\rebirth`,
   `python tools/stub_data.py pack build\stub\ei-stub-data.tar.zst build\stub\vanilla build\stub\rebirth`.
   The folder names must stay `vanilla` and `rebirth` (the job looks for them); pack needs Python 3.14 (its zstd).
2. Optionally encrypt it (Git for Windows' openssl, in its bash): a key `export EI_KEY=$(openssl rand -base64 32)`, then
   `openssl enc -aes-256-cbc -pbkdf2 -iter 200000 -salt -in build\stub\ei-stub-data.tar.zst -out ei-stub-data.tar.zst.enc -pass env:EI_KEY`.
   A file over GitHub's 100 MB goes in parts: `split -b 90m -d <file> <file>.` (names `<file>.00`, `.01`, ...).
3. Create a private repository on GitHub (e.g. `DoctorGester/ei-stub-data`), and push the file(s) to its default
   branch at the top level: `ei-stub-data.tar.zst` (or `ei-stub-data.tar.zst.enc`, or their parts). A new set later: a
   new commit replacing them (the job takes the default branch's).
4. Create a fine-grained personal access token (GitHub > Settings > Developer settings > Fine-grained tokens):
   resource owner your account, "Only select repositories": the stub data repository alone, permissions: Contents
   read-only (Metadata read-only comes with it), an expiry you will renew.
5. In this repository's Settings > Secrets and variables > Actions: the secret `EI_STUB_DATA_TOKEN` (the token), the
   secret `EI_STUB_DATA_KEY` (the key of step 2, only when encrypted), and under Variables the repository variable
   `EI_STUB_DATA_REPO` = `owner/name` of the private repository. The variable switches the job on; deleting it
   switches it off.
6. Start it once by hand (Actions > Suites (Linux) > Run workflow) and read the summary's goldens blocks.

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
