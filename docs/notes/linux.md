# Linux

The game builds on Linux (x86-64, gcc 13 or clang 18) from the same CMakeLists.txt as on Windows: the game and
library code unchanged, SDL3 for the window, input, sound and threads, POSIX for the rest of the platform
(src/platform/posix, one file per area), and the renderer on SDL_GPU's Vulkan driver (src/render/gpu.cpp;
docs/notes/renderer.md section 15c). The harness builds too (`-DEI_HARNESS=ON`, "The harness" below); the offline
checks against Windows' drawing and keys stay Windows' only.

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

- `port_platform` (tests/port/port_check.cpp): path folding, the Bink container read through platform::File, files and
  mappings, .ini reads and writes, the one-instance lock, UDP, the settings document's checks
  (tests/unit/settings_file.cpp), lib/maths.h's functions where glibc's and Windows' own float functions round
  otherwise (tests/unit/maths.cpp).
- `port_render` (tests/port/render_check.cpp): the interface's glyph cases on Vulkan against their CPU reference
  (tests/render/glyph_cases.cpp, which tools/ui_check runs on Direct3D 12; one level a channel for each blend at a
  pixel, as lavapipe blends 8-bit targets in fixed point: glyph_cases.h), the game window's pixel size and client /
  screen round trip, the cursor's hold dropping the motion from before the press (macOS runs these too, macos.md), and
  the window's present after resizes: a frame of known rows presented through the swap chain at four sizes, the window
  read back from the X server (XGetImage).
- `port_shaders` (tools/shader_check): every HLSL program and its pipelines on SDL_GPU's Vulkan driver (lavapipe
  without a GPU), with `--require`: no Vulkan device fails.
- `port_gpu` (tools/gpu_check): the renderer's SDL_GPU layer (src/render/gpu.cpp; renderer.md section 15c) on the
  same driver: textures, buffers, draws in order, downloads, the upload ring, the swap chain on a small window of the
  virtual display; `--require` as above.
- `port_input` (tools/input_check/input_check_linux.cpp, `ei_input_check`): the keyboard's layout codes, Ctrl
  characters and key names ("What differs from Windows" below) on X11's us, ru, fr and de layouts, every key a layout
  moves against a written table (1013 checks). It starts an Xvfb of its own (from :99 down, as run_all) and sets each
  layout there with `setxkbmap -display`, so it never changes the session's layout; skipped (exit code 77) without
  Xvfb or setxkbmap (x11-xkb-utils, which Xvfb's packages bring). `ei_input_check --print` prints what each layout
  gives: the hand check of another layout (add it to the list, compare with what its keys type).

The Linux workflow's build job (.github/workflows/suites-linux.yml) runs them on GitHub's Ubuntu runner for every push.

## Run

`build/linux/ei-rebuild` plays the game folder named by `EI_GAME_ROOT`, else the one it sits in; `EI_SETTINGS_PATH`
pins the settings file. The folder's `Work path` is read as the game folder itself. Game data names are matched as
Windows matches them: `res\database.res` opens `Res/Database.RES` (one folder at a time; the exact spelling first).
Arguments for the game follow `--`. Under WSLg Mesa draws on llvmpipe (the CPU) unless `GALLIUM_DRIVER=d3d12` puts it
on the Windows GPU.

## Release

The Release workflow (.github/workflows/release.yml, run by hand) builds Linux beside Windows and puts
`ei-rebuild-linux-x64.tar.gz` (`ei-rebuild-linux-x64/`: `ei-rebuild`, `ei-convert`, `examples/`, `README.txt`,
`third_party_notices.txt`) on the same GitHub release. Its `linux` job runs in an `ubuntu:22.04` container so that the
binary needs no newer glibc than 22.04's 2.35: gcc 13 from the ubuntu-toolchain-r PPA (22.04's gcc 11 has no
`<format>`) with `-static-libstdc++ -static-libgcc`, cmake from pip (CMakeLists.txt wants 3.24, 22.04 has 3.22),
`-DCMAKE_BUILD_TYPE=Release`, the port checks under `xvfb-run`, then `strip`. The shader toolchain cannot run in
there: DXC's Linux release (`libdxcompiler.so`, `libdxil.so`) needs glibc 2.38. So a `linux-shaders` job on the
ubuntu-24.04 runner itself builds just the target `ei_shaders` (`shaders/gpu_shaders.h` and `.cpp`), uploads the two as
an artifact, and the container job configures with `-DEI_PREBUILT_SHADERS=<their folder>`, which copies them into its
build folder instead of fetching and running the toolchain (no DXC, no Python needed). The two files are the same on
any Linux (SPIR-V and MSL bytes). The Package step fails on any `GLIBCXX_` import, writes the newest `GLIBC_` version
the binary imports into the run's summary and the README, and lists its libraries: `libGL.so.1`, `libm`, `libc` (SDL3
is static and opens X11, sound and the rest itself).

glibc: built on 24.04 the same steps need 2.38 (`__isoc23_*` and `strl*` from 2.38's headers, `arc4random` 2.36,
which SDL finds at configure time); on 22.04's headers those are absent, and what remains tops out at 2.35
(`_dl_find_object` from the static libgcc unwinder, `hypotf`), so the oldest systems are those with glibc 2.35:
Ubuntu 22.04, Debian 12, Fedora 36, not RHEL 9 (2.34).

## The harness

`cmake -S . -B <build> -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEI_HARNESS=ON` builds `ei-rebuild` with the harness
(tests/verify: its platform backend and its own system layer's system_posix.cpp; harness.md, "The harness's code"). The
Linux workflow builds it. Scenarios run with run_all.py as on Windows (`python3 tools/run_all.py ...`), with
`EI_BUILD_DIR` the build folder, `EI_GAME_VANILLA` / `EI_GAME_REBIRTH` the game folders (copies: a run never writes
into them, but WSL reads /mnt slowly), and `EI_HARNESS_SETTINGS=<checkout>/config/harness` when the build folder is not
two levels below the checkout. Under WSL keep the checkout and the build in WSL's home (gcc over /mnt runs at a quarter
of the speed).

run_all's machine slots are the system's own (lock files in its temp folder, Linux's /tmp under WSL): games in WSL do
not count against Windows' 16, so under WSL run_all allows 6 at once on lavapipe by default (WSL's memory is capped at
16 GB; `EI_HARNESS_SLOTS` overrides it), and a local Linux suite on lavapipe runs only while no Windows suite does; on
dzn, the default under WSL once it is installed, run_all holds Windows' slots itself and takes its games from them
("The GPU under WSL"). Without scenario names a Linux run leaves out what its driver cannot match (run_all's
`LINUX_LEFT_OUT`: on lavapipe the pairs, render_batch, camera_batch, render_stats and zone_gz15h_tornado; on dzn
net_pair_master; on both the Direct3D 12-only `render_device_lost_*`).

On Linux run_all starts one Xvfb itself (a free display number from :99 down, a 3840x2160 screen like the goldens'
Windows desktop, DISPLAY of every game pointed at it, WAYLAND_DISPLAY unset; harness.md) unless `EI_SHOW_WINDOWS=1` is
set, so a plain run opens no window on the desktop under WSLg either, and Mesa draws on llvmpipe as in CI; wrapping
run_all in `xvfb-run` with `-screen 0 3840x2160x24` as CI does is the same thing (with `EI_SHOW_WINDOWS=1`). (Xvfb's own
`-displayfd` choice of a number fails under WSL, where /tmp/.X11-unix is WSLg's; run_all names the number.)

Since lib/maths.h (float-maths.md, "Maths functions") the Linux runs give the Windows goldens digest for digest, record
0 included, on lavapipe and dzn alike. What still differs, and why:

- The display's size. The scenarios with `resolution desktop` (ui_widescreen, ui_widescreen_dialogue, _hud, _more) take
  the screen's size and their goldens are those of a 4K desktop: on a smaller Xvfb a click lands elsewhere. run_all's
  own Xvfb has the goldens' size.
- Time limits under load: llvmpipe draws a 4K frame in 40 ms and more, so with 4 games at once the longest runs
  (camera_drag_rate) reach their limits and some pass only on run_all's retry.
- The pair scenarios (net_pair_*): llvmpipe draws a frame step in about 140 ms (6 ms on Windows' GPU), so a pair's run
  passes its frame waits and its time limit and the turns fall differently; left out on lavapipe. On dzn they pass;
  net_pair_master's peer lists the server one poll sooner there, as on macOS.
- render_terrain_seams' level steps (in render_batch): on lavapipe 0.91% and 0.72% against at most 0.3%; lavapipe's
  level choice for the anisotropic, linearly mipmapped terrain sampler steps more often than a GPU's. Passes on dzn.
- The depth copy (renderer.md, "The depth copy"): lavapipe blends the multisampled pass's second target with the first
  one's blend state (SRC_ALPHA / INV_SRC_ALPHA of the game's blended draws) whatever the pipeline says, and a
  one-component output has alpha 0 there, so every blended draw left the copy at its clear value (the water then drew
  over the land in `smoothorder split`). The copy's output has four components, alpha 1 (depth_copy.hlsli,
  into_multisampled.hlsl; the target stays R32F), so that blend stores the depth. The Vulkan validation layer reports
  nothing, and a stand-alone Vulkan program with the same states writes the second target, so what in the game's draws
  brings it out is not known.
- Sample counts: lavapipe lists 1 and 4 samples; the world draws with the device's nearest count (`SamplesFor`,
  render.cpp), so `msaa 2` and `msaa 8` give 4 there.
- A refused connect (net_second_master): a TCP connect to a closed loopback port is refused at once on Linux, Windows
  tries again for about a second, so the connection screen's master request box ("masterbox") closes by itself before
  the scenario cancels it; the scenario's Esc is `ifwindow masterbox key esc`.
- `windowsize` resizes asynchronously under X11 (SDL_SetWindowSize returns before the window has its size;
  SDL_SyncWindow does not wait there either), so the step (harnessplatform::ResizeClient) pumps events until the window
  has the size, at most 2 s.
- A cut `frames` step: its real-time limit is 10 s plus 100 ms a frame; past it the step fails and the scenario goes
  on with frames still to go, and as each frame is five world updates the later steps land on earlier world updates
  and the world takes another course (render_stats on a 2-core runner: an attack's skill +3.4e38 against -3.4e38 from
  record 63 on, a resolved hit's and miss's marks, combat.cpp kHitMark / kMissMark). render_stats matches with 4 cores
  or more but takes over 60 s on llvmpipe, so local runs leave it out.
- The time zone. A save's info.sav keeps mktime of the harness's fixed date (2001-03-26 12:00), which followed the
  machine's zone (the build machine's Europe/Prague, UTC+2 that day, gave the goldens' 985600800). The harness sets
  the process's zone to a fixed UTC+2 (`TZ=EET-2`) with the fixed date (harness_clock.cpp), so it is the same in any
  zone.

Harness runs open no sound device; `EI_HARNESS_SOUND=1` keeps the real one (sound.md, "How the sound keeps up").

### The GPU under WSL

WSL gives Linux the GPU only as Direct3D 12 (/dev/dxg, the libraries in /usr/lib/wsl/lib); Ubuntu's Mesa has no Vulkan
driver for it, so the games draw on lavapipe (the CPU, about 12 cores a game). Mesa's Dozen (dzn: Vulkan on D3D12)
puts them on the Windows GPU. It is built from Mesa's release into `~/mesa-dzn` in the WSL home, never system-wide
(Mesa 26.2.6, about 10 minutes with 8 jobs; the build tree `~/mesa-dzn-src` can go afterwards):

```sh
sudo apt-get install python3-mako python3-yaml python3-venv bison flex glslang-tools libdrm-dev libx11-xcb-dev \
    libxcb-dri3-dev libxcb-present-dev libxcb-shm0-dev libxcb-xfixes0-dev libxcb-randr0-dev libxcb-sync-dev \
    libxcb-keysyms1-dev libxshmfence-dev libxxf86vm-dev libexpat1-dev libzstd-dev libelf-dev zlib1g-dev vulkan-tools
mkdir -p ~/mesa-dzn-src && cd ~/mesa-dzn-src
curl -sSfLO https://archive.mesa3d.org/mesa-26.2.6.tar.xz && tar xf mesa-26.2.6.tar.xz
python3 -m venv --system-site-packages venv && venv/bin/pip install meson   # 24.04's meson is too old
cd mesa-26.2.6
# The one local change: dzn keeps libd3d12 loaded when its instance ends (below).
python3 -c "p = 'src/microsoft/vulkan/dzn_device.c'; s = open(p).read(); open(p, 'w').write(s.replace(
    '   if (instance->d3d12_mod)\n      util_dl_close(instance->d3d12_mod);\n', '   /* kept loaded */\n', 1))"
../venv/bin/meson setup build --prefix=$HOME/mesa-dzn --libdir=lib -Dbuildtype=release \
    -Dvulkan-drivers=microsoft-experimental -Dgallium-drivers= -Dplatforms=x11 -Dllvm=disabled -Dopengl=false \
    -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dgles1=disabled -Dgles2=disabled -Dmicrosoft-clc=disabled \
    -Dvideo-codecs= -Dvulkan-layers= -Dtools= -Dbuild-tests=false
ninja -C build -j 8 && ninja -C build install
```

The local change: SDL_GPU's driver probe makes a Vulkan instance and ends it before the real one, and dzn's instance
end closed libd3d12, which took libd3d12core with it while one of its threads still ran (a SIGSEGV at an address in no
module, before "gpu: vulkan device", in about a third of the runs; preloading libd3d12.so instead crashes every
program at its start). Kept loaded, as dzn already keeps libdxcore, no run crashes.

The driver's manifest is `~/mesa-dzn/share/vulkan/icd.d/dzn_icd.x86_64.json`; `VK_DRIVER_FILES` set to it puts any
program on dzn (`vulkaninfo --summary`: "Microsoft Direct3D12 (NVIDIA GeForce RTX 4070 Ti SUPER)", Vulkan 1.2, driver
Dozen; every program prints "dzn is not a conformant Vulkan implementation, testing use only"). The gallium d3d12
driver (OpenGL on D3D12) is not built: the game draws through SDL_GPU's Vulkan only. SDL_GPU lacks nothing it uses
there: 4 samples at most (8 asked gives 4, as on lavapipe), BC textures, anisotropy, the three present modes; the port
checks pass on it.

Under WSL run_all points the games at it by itself when that manifest exists (`vulkan_driver()`); `--lavapipe` or
`EI_VK_ICD=system` keeps the system's driver, `--dzn` or `EI_VK_ICD=dzn` asks for dzn anywhere on Linux, and
`EI_VK_ICD=<manifest>` names any driver. GitHub's runner is no WSL: CI stays on lavapipe, its reference. Those games
draw on Windows' GPU, so under WSL run_all then also holds Windows' machine slots for them (`hold_windows_slots()`:
Windows' python.exe through WSL's interop runs run_all.py `--hold-slots N [--hold-most M]`, which takes lock files in
Windows' temp folder, the same as Windows' suites and work.py's scheduler, until the run ends), so Windows' and WSL's
games together stay within Windows' 16, and runs no more games at once than it holds. With `--jobs N` it waits for N
slots (2N with a pair in the run), fixed to the run's end; without, it waits for 2 (a pair's) and takes every other one
free at that moment up to `WSL_GPU_SLOTS`, 8 (`EI_HARNESS_SLOTS` sets the most instead): half the machine, so Windows'
suites keep the other half however long the WSL run lasts. Then the hold follows the run's need (`follow_need()`,
every 2 s): the games it still has to run (running and queued, a pair's two) are the slots it wants, at most the most.
More than it holds: it sends `want <n>` on the Windows process's stdin, which takes the free slots one at a time
without waiting (`adjust_hold()`) and answers `held <m>`, and the games' limit (`GameSlots`, in place of run_all's own
slot pool) grows to m; so a run started while Windows' 16 are busy begins with 2 and grows as they free. Fewer: the
limit drops to the games running (never below), then `want` gives the rest back, so the last games of a run do not
hold slots they no longer use. Checked in `tools/test_work_sched.py` (hold_slots, adjust_hold, GameSlots, rebalance,
follow_need). (Windows' Python started from WSL works in a `\\wsl.localhost` folder, where `/proc/version` is WSL's:
`under_wsl()` asks Linux first, or that side took WSL's pool of 6 as the most.)

On dzn the image suite takes about 2 minutes with 8 slots on an idle machine (lavapipe about 7 with 4); the GPU shared
with Windows' games costs more than the jobs gain (three times as long beside 8 Windows games). WSL's memory: a game's
own RSS is 150 to 250 MB, but dzn's GPU memory counts in WSL's "used" too, about 1.2 GB a game: 8 games peaked at 12 GB
of WSL's 16, so 8 is also the most that fits. dzn turns down new fences after a few seconds of several games when
none go back (renderer.md, "Fences").

### Sound under WSLg

WSLg's only output is pulseaudio's "RDP Sink" (`PULSE_SERVER=unix:/mnt/wslg/PulseServer`), and on the build machine it
plays slower than real time: the latency it reports grows by 80 to 90 ms each second, and at 1.1 to 1.7 s of backlog,
13 to 18 s in, it nearly stops taking anything; a game started a little later finds it stalled from the start. A plain
libpulse writer (pa_simple, no SDL, no game) does the same, so it is the WSLg / RDP path, not ours. The game's sound
thread never waits for the device (sound.md), so a stalled sink leaves the game silent but running at its frame rate.

## The stub data

The suites can run without the game's files: `tools/stub_data.py build <install> <folder>` writes a copy of an install
with every picture, sound and movie blanked in its own format, so the game reads it through its ordinary loaders, and
the goldens stay those of the install. The repository is public: the stub set is still the game's data (maps, models,
scripts, texts) and lives only under `build/` and in a private repository, never in git here, a public release or an
Actions cache.

What the runs read (`EI_READTRACE=1`, summed by `stub_data.py reads`; both suites): from Res\ every archive the game
opens at start (aiinfo, database, databaselmp, figures, gfx, materials, menus, outro, redress, sfx, speech, texts,
textslmp, textures) and music.reg, smessbase.reg, streamsn.reg, sysmsg.txt; the maps the scenarios play (.mpr, .mob,
every .mq); camera\mainmenu.cam and a zone's .cam; config\ai.reg and the lights .ini files; the loading screen's, the
titles' and the credits' movies (progres*.bik, titles.bik, crdtfin.bik); the music the zones choose (stream\*.mp3); on
Rebirth also EI_Plugin.ini, 2utoexec.cfg and starter\mods\standard\config.reg. Looked for and absent: autoexec.cfg,
the loose textures\*.bmp overrides, the mod folder's copies, and starter\addon.dll / EI_Plugin.asi (whose presence
picks the Rebirth profile). Never read: the install's saves\ (a run's are its own folder's), its programs, the
starter's own files.

What of them reaches game state, and so what the stubs keep:
- Kept whole: maps (terrain, sectors, objects, scripts, quests), figures (geometry, skeletons, animations and their
  events), databases, texts, registry files, cameras, configs, fonts, the Rebirth mods.
- Textures (.mmp, in any archive and inside nested ones: .mpr, .mod are archives too) and loose .bmp: the header (size,
  format, levels) and the .mmp's extra block; the pixels 0. Kept whole (`KEPT` in stub_data.py, by entry name): the few
  whose pixels a frame check reads and which fail on blanks. A new frame check that fails on blanks adds its textures
  the same way: a list, not a pattern (a pattern would move every other frame check's pixels and thresholds). Find a
  check's textures by logging the texture each mesh draw bound (a composed texture's layers: `FindLayers`' `found`,
  texture_load.cpp), or by painting candidates one colour each in the stub set. The list and why:
  - the cursor pictures (`cursor_*`, `sm_cursor_*`): render_cursor_present wants opaque and clear pixels and a shape
    that changes with the gamma;
  - the faces (`*mouth*`): render_blink's open and closed rectangles;
  - bz1g's ground `basegipat000`, `001` (render_shadows_outside: the gate's shadow on lit ground, 85% of its pixels)
    and `005` (render_weather_roofs: the ground under the gateway's floor);
  - `dgunbirdruins00`, `dguntorchruins01`, `ruins00`, `tree02`: the models in view at zone1's start, whose pixels
    render_smooth's msaa and anisotropy checks need to change with the filter;
  - `footprints*`, `bloodprints`, `fireprints`, zone2's ground pages `zone2000`-`2007` and the thirteen layers of the
    hero's skin (`unhuma*`): render_shadows_decals' prints and printlight (a print's brightness against its ground) and
    render_weather_panel's wet figure, on both sets;
  - zone1's pages `zone1000`-`zone1002` (vanilla only; the pattern `zone100[0-2]` would take zone10's): render_water's
    ripples between two frozen moments (the water layer draws the ground's page textures again, so black pages leave
    the ripples' shading small);
  - `rainsnow.mmp`: the rain and snow particles (render_weather's particles on / off frames).
  Rebirth's textures.res stores some names twice over one range (`Zone2000` and `bz2g000`, `Zone2003` / `bz2g003`,
  `Zone2005` / `bz2g005`: one entry's bytes, two names), so `stub_in_place` keeps every name of a kept entry's range
  (blanking the other name zeroed the kept pixels). Check after a repack: the kept entries' bytes equal the install's.
  render_water's margin on GitHub's runner is the open question: the kept pages bring the stubs to 90% of the
  install's moving-pixel share.
- Sounds (.wav): every chunk; the samples 0. A voice ends by its data's bytes over its byte rate (sound.md).
- MP3 (speech, music): every frame header (the game counts frames: ScanMp3), the frames' data 0, an info frame whole.
- Movies (.bik): header and frame index (frame count, size, rate, keyframes); every frame emptied (no sound packet, no
  video packet: the decoder keeps the frame before). The game reads a movie's frame numbers, never its pixels.
- Programs (.exe, .dll, .asi): empty files, for the profile's presence check.

On the stub sets both suites give the installs' goldens and every run passes its own checks, on Windows and on Linux.
`stub_data.py check <stub> <install> <runs>` confirms every file the runs read is in the stub set at the install's size
(the movies aside).

Sizes: the vanilla set 724 MB on disk (121 MB of it not zero: figures.res 53, the maps 59), Rebirth's 1850 MB (240 MB
not zero): its high-resolution textures.res is 655 MB of blanked pixels and the LostInAstral mod 711 MB (its speech and
sfx blanked; 119 MB of maps and figures that no run reads). The blanks cost next to nothing packed; truncating them
would change the archives' layout and only save the runner's disk. `stub_data.py pack` (Python 3.14's zstd) puts both
in one .tar.zst of 93.0 MB: level 22 with a 128 MB window and long matching, the two installs' copies of a file next
to each other (xz -9 gives 65 MB but unpacks in 7 to 9 s against zstd's 1 s). That is under GitHub's 100 MB file
limit; the next addition to Rebirth's set may need the split parts the CI job unpacks (`ei-stub-data.tar.zst.00`,
`.01`). Building takes about 20 s an install, packing 5 minutes.

### The CI job

`.github/workflows/suites-linux.yml` ("Linux") has two parts. Its build job builds the game and the harness with
warnings as errors and runs ctest (above), with no game data; it saves its compiler cache under the commit. Then eight
suites jobs in parallel each restore that cache (the harness build is all hits and a link), check out the private
repository with a read-only token, unpack the stub set into the runner's temp folder, remove the checkout and play
every eighth scenario of the sorted list (both suites, one game at a time) under xvfb on llvmpipe, less every
render_*, net_*, camera_* and mod_lost_in_astral_* scenario (the user: the renderer's looks, the two-game, camera and
Lost in Astral runs were most of the time and the failures; named in the `scenarios` input they still run) and
`LINUX_SKIP` (ui_minimap_edges, the Rebirth widescreen layouts, unit_ai_bridge, zone_gz15h_tornado_client: they cannot
match on llvmpipe or take more than three times their limit there); a job fails when a golden differs. It runs on
pushes to main and `claude/**` (not docs-only ones), on pull requests from this repository's own branches (a fork's
gets no secrets and the suites are skipped) and by hand; it uploads nothing but the build job's two shader files, and
the data is outside every cached path. The suites are off until the variable below is set.

A third part, the gcc-latest job, compiles the game and the harness (no checks, warnings not errors) in the unpinned
`gcc:latest` image with the build job's shaders (EI_PREBUILT_SHADERS): every gcc release drops some of libstdc++'s
indirect includes, so a file that uses a name without its header builds on gcc 13, Apple clang and MSVC and breaks on
the newest gcc (gcc 16: std::find_if without `<algorithm>`). Include the header of every std name a file uses. A local
check on macOS: configure with `-DCMAKE_CXX_FLAGS=-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES`, which drops libc++'s
compatibility includes (libc++'s and libstdc++'s leftover includes differ, so each catches other files).
The Windows twin (suites-windows.yml: windows-latest on D3D12 WARP) is a smoke job like the macOS one; harness.md, "The
Windows CI job".

`EI_TIME_SCALE` (3 in the workflow; default 1, never below 1) multiplies the harness's real-time limits: a `frames`
step's (10 s plus 100 ms a frame), the real-time safety limit of the other waits (four times the wait plus 10 s) and
the scenario's (60 s or its `timeout`). The game reads it (harness.cpp, `TimeScale`) and so does run_all.py
(`time_scale`, for the limit it reports and enforces); nothing that counts the world's clock changes, so a run that is
not cut is the same at any scale. The reason: the runner's speed varies (the same scenarios 9% slower from one run to
the next, some 40 to 90%), so every failure was a real-time limit of a slow runner and adding each to `LINUX_SKIP`
never converged; a cut frames step also changes the digest after it ("The harness"). Every frame of a virtual `wait` is
drawn, so on llvmpipe a scenario's real time follows its waits (about 0.2 s a frame on 4 cores whatever the window's
size: the game's updates, vertex work and uploads); a scenario that needs more gets a `timeout` line of its own, which
the scale multiplies.

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

- The keys (src/platform/posix/keyboard.cpp, the same on macOS): there are no Windows layouts to give the virtual-key
  codes, so `LayoutKey` takes them from what the key types in SDL's keymap (`SDL_GetKeyFromScancode`): on a layout
  whose letter keys type Latin letters a letter's or digit's code and the US punctuation characters' codes
  (`sdl_keys::KeyOfTyped`: AZERTY's M key VK_M, its comma key VK_OEM_COMMA), on another (Russian, Greek) the US codes,
  as Windows' Russian layout has them (SDL's event key codes are the US layout's there). That is Windows' code for
  every key on us and ru; Windows' French and German layouts give some punctuation keys other VK_OEM codes, and
  German's ß and - keys share VK_OEM_MINUS here (no game code reads the VK_OEM codes: bindings and the console go by
  scan code). Ctrl: a letter key its control character, where the layout's key types `[`, `\` or `]` ESC, FS or GS,
  else none (Russian's х and ъ keys none, as on Windows). Key names as Windows' GetKeyNameTextW gives them: a letter
  key its code's letter (Russian's Й key "Q", as on Windows), another moved key what it types (х, ж, ё; French's
  digit row & é "), the rest Windows' US names ("Num 7", "Right Ctrl"): neither SDL nor xkb has key names in the
  layout's language. A dead key types no character in SDL's keymap and keeps the US key's name (French ^ is "[").
- Not played by hand with real data yet (the harness plays the installs' copies): menus, maps, movies, sound, saves
  and multiplayer are unchecked by a player on Linux.
- Frame pacing under WSLg (X11 through XWayland, GALLIUM_DRIVER=d3d12, VSync on): 64 to 85 frames a second, 10 to 28 ms
  apart, where Windows' 60 Hz screen gives 16 / 17 ms; on a busy machine 32 to 38, 16 to 66 ms apart. XTest pointer
  moves at 125 Hz arrive as 4, 8 or 12 counts a frame: the beat between the mouse's reports and the frames. The
  camera's mouse turn takes the mouse's speed over a 60 Hz frame's window (ui.md, "Camera mouse turn"), so it follows
  neither. The SDL build in WSL has no Wayland backend (no wayland-client development files), so
  SDL_VIDEO_DRIVER=wayland was not measured.
