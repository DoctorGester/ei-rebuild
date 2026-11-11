# Linux

The game builds on Linux (x86-64, gcc 13 or clang 18) from the same CMakeLists.txt as on Windows: the game and
library code unchanged, SDL3 for the window, input, sound and threads, POSIX for the rest of the platform
(src/platform/posix, one file per area), and the renderer on SDL_GPU's Vulkan driver (src/render/gpu.cpp;
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
pins the settings file. The folder's `Work path` is read as the game folder itself. Game data
names are matched as Windows matches them: `res\database.res` opens `Res/Database.RES` (one folder at a time; the
exact spelling first). Arguments for the game follow `--`. Under WSLg Mesa draws on llvmpipe (the CPU) unless
`GALLIUM_DRIVER=d3d12` puts it on the Windows GPU.

## Release

The Release workflow (.github/workflows/release.yml, run by hand) builds Linux beside Windows and puts
`ei-rebuild-linux-x64.tar.gz` (`ei-rebuild-linux-x64/`: `ei-rebuild`, `ei-convert`, `examples/`, `README.txt`,
`third_party_notices.txt`) on the
same GitHub release. Its `linux` job runs in an `ubuntu:22.04` container so that the binary needs no newer glibc than
22.04's 2.35: gcc 13 from the ubuntu-toolchain-r PPA (22.04's gcc 11 has no `<format>`) with `-static-libstdc++
-static-libgcc`, cmake from pip (CMakeLists.txt wants 3.24, 22.04 has 3.22), `-DCMAKE_BUILD_TYPE=Release`, the port
checks under `xvfb-run`, then `strip`. The shader toolchain cannot run in there: DXC's Linux release
(`libdxcompiler.so`, `libdxil.so`) needs glibc 2.38, so linking shadercross failed at configure (release run
113147799174). So a `linux-shaders` job on the ubuntu-24.04 runner itself builds just the target `ei_shaders`
(`shaders/gpu_shaders.h` and `.cpp`), uploads the two as an artifact, and the container job configures with
`-DEI_PREBUILT_SHADERS=<their folder>`, which copies them into its build folder instead of fetching and running the
toolchain (no DXC, no Python needed). The two files are the same on any Linux (SPIR-V and MSL bytes). The Package step fails on any `GLIBCXX_` import, writes the newest `GLIBC_`
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
/tmp under WSL): games in WSL do not count against Windows' 16, so under WSL run_all allows 6 at once on lavapipe by
default (WSL's memory is capped at 16 GB; `EI_HARNESS_SLOTS` overrides it), and a local Linux suite on lavapipe runs
only while no Windows suite does; on dzn, the default under WSL once it is installed, run_all holds Windows' slots
itself and takes its games at once from them ("The GPU under WSL"). Without scenario names a Linux run leaves out the
scenarios its driver cannot match (run_all's `LINUX_LEFT_OUT`: on lavapipe CI's `LINUX_SKIP` and render_stats and
zone_gz15h_tornado and the two frame checks below, on dzn net_pair_master; the list below says why). Under WSL keep the checkout and the build in WSL's home
(gcc over /mnt runs at a quarter of the speed).
On Linux run_all starts one Xvfb itself (a free display number from :99 down, a 3840x2160 screen like the goldens' Windows desktop, DISPLAY of every game
pointed at it, WAYLAND_DISPLAY unset; harness.md) unless `EI_SHOW_WINDOWS=1` is set, so a plain run opens no window on the desktop under
WSLg either, and Mesa draws on llvmpipe as in CI; wrapping run_all in `xvfb-run` with `-screen 0 3840x2160x24` as CI does is the same thing (with `EI_SHOW_WINDOWS=1`). (Xvfb's own
`-displayfd` choice of a number fails under WSL, where /tmp/.X11-unix is WSLg's; run_all names the number.)

Against the goldens (H47d, 2026-11-03, WSL2 with 32 cores and 16 GB, Xvfb at 3840x2160, SDL_GPU on lavapipe; the image
suite less LINUX_SKIP and camera_smooth, camera_spin, render_stats and zone_gz15h_tornado, the Rebirth set less its
pairs): the image suite 139 runs, 133 the same, 6 without a golden (render_resize_edgepan, render_resize_zone,
render_shadows_trees, watchdog_*), none differing, in 438 s with 4 jobs (228 s with 16 jobs, where ui_describe and render_shadows_outside timed out too);
the Rebirth set 19 of 19 the same, in 195 s. That is digest for digest the Windows goldens, record 0 included, since
lib/maths.h (float-maths.md, "Maths functions"). What still differs, and why:

- The display's size. The scenarios with `resolution desktop` (ui_widescreen, ui_widescreen_dialogue, _hud, _more) take
  the screen's size and their goldens are those of a 4K desktop: on Xvfb at 1280x1024 three of them differ (a click
  lands elsewhere, the digest differs from tick 100 to 580), at 3840x2160 they are the same. run_all's own Xvfb has that
  size.
- Time limits under load: llvmpipe draws a 4K frame in 40 ms and more, so with 4 games at once camera_drag_rate (120 s
  limit, 113 s used) is cut at its limit and levers_key and ui_widescreen (Rebirth) pass only on their retry (60 s limits); alone, camera_drag_rate takes 86 s. The retry is run_all's own.
- The pair scenarios (net_pair_*; net_pair_master's peer lists the server one poll sooner): llvmpipe draws a frame
  step in about 140 ms (6 ms on Windows' GPU), so a pair's run passes its frame waits and its time limit (about 190 s
  against Windows' 30 to 50 s) and the turns fall differently. Not run again: they exceed the 60 s game limit there.
  On dzn ("The GPU under WSL") they take 6 to 56 s and all pass, six the same as their goldens.
- A frame, not a digest (differs on lavapipe only; on dzn it passes, so it stays in `LINUX_LEFT_OUT` for lavapipe).
  render_terrain_seams: the level steps on the vanilla data, 0.91% and 0.72% against at most 0.3% (the same with the
  stub set, 0.89% and 0.72%): the terrain's sampler is anisotropic at 16 with linear mips and lavapipe's level choice
  steps more often than a GPU's.
- render_smooth ran on lavapipe once two faults were found (H47b1; it is in the suites again). (1) The split pass's frame
  (`smoothorder split`, a draw order the game never reaches) differed from the smooth frame in 37% of the pixels, the
  whole world view darker and greyer. Cause: on lavapipe the depth copy, the multisampled pass's second colour target
  (R32F, written by every world pipeline, renderer.md "The depth copy"), was never written by the alpha-blended draws,
  which is all of them: lavapipe blends the second target with the first's blend state (SRC_ALPHA / INV_SRC_ALPHA)
  although the pipeline gives it its own (blend off, or ONE / ZERO: no difference), and a one-component output has
  alpha 0 there, so the copy kept its clear value, depth 1, in every frame. The resolved depth copy is what sets the
  frame target's depth after the world; at 1 everywhere, the split's second run (the water, drawn last) passed its
  depth test over the land. In the game's own order the depth only reaches the interface's depth-tested draws, so no
  frame showed it. Seen by drawing the resolved copy as the frame's colour (all clear value on lavapipe, the
  terrain's depth on dzn), by changing the clear value (the frame took it), by blending off the first target (the copy
  was written) and by an RGBA32F copy with alpha 0.5 (the stored depth was exactly the half-and-half mix with the
  old value: blended with SRC_ALPHA). The Vulkan validation layer reports nothing for it; a stand-alone Vulkan program
  with the same render pass and blend states does not show it (the second target is written there), so what in the
  game's draws brings it out is not known. Fix: the depth copy's output has four components, alpha 1
  (depth_copy.hlsli, into_multisampled.hlsl; the target stays R32F): the blend then stores the depth, and where the
  pipeline's own state is used there is no blend, so the same everywhere (the goldens hold on Windows). (2) `msaa 2`
  drew with 2 samples, which lavapipe does not list (framebuffer sample counts 1 and 4; the validation layer says
  `VUID-VkImageCreateInfo-samples-02258`): the frame was black (89% of the pixels differ from plain). The world is now
  drawn with the device's nearest count (`SamplesFor`, render.cpp: the least it has at or above the asked one, else the
  most below; `gpu::SupportsSamples`), so `msaa 2` gives 4 there. Now the split frame differs in 0.065% of the pixels
  (largest 108; the check allows 0.2%), the same on dzn, and render_smooth passes on lavapipe with the real data,
  vanilla and Rebirth.
- The saved picture (render_smooth, ui_quicksave: "94% of the pixels differ" under GL, H47): gone since SDL_GPU. On
  lavapipe `checksave` passes in both, with the real data and the stub set; ui_quicksave runs in the suites again.
- A refused connect (net_second_master): a TCP connect to a closed loopback port is refused at once on Linux, Windows
  tries again for about a second, so the connection screen's master request box (MasterRequestBox, "masterbox") closes
  by itself before the scenario cancels it. The scenario's Esc is now `ifwindow masterbox key esc`: pressed only while
  the box is on top (Windows), skipped otherwise; the goldens are the same either way. (The `wait 2500` before it
  stays: it runs on the harness's clock.)
- render_present_window: `windowsize` resized asynchronously under X11 (SDL_SetWindowSize returns before the window
  has its size; SDL_SyncWindow does not wait there either), so the clicks after it aimed at a window that had not
  changed yet. The step (harnessplatform::ResizeClient) now pumps events until the window has the size, at most 2 s.
- Over 60 s on llvmpipe, left out: camera_smooth, camera_spin, render_stats, zone_gz15h_tornado (not run in H47d;
  since H74 the camera ones and render_terrain_seams are sections of camera_batch and render_batch, which
  LINUX_LEFT_OUT names). On dzn they take 7 to 22 s and are the same as their goldens. CI run 41857898300 (8 shards, 2026-11-04) also cut
  camera_drawn and render_shadows_outside (frame waits), spells_more and levers_key (Rebirth) at their limit: all
  six were in LINUX_SKIP (H58 took them out under EI_TIME_SCALE). render_stats's run there also differed from record 63 on (an attack's skill +3.4e38 against
  -3.4e38): only a consequence of its cut (H56). A `frames` step has a real-time limit, 10 s plus 100 ms a frame (16 s
  for 60); past it the step fails and the scenario goes on with frames still to go, and as each frame is five world
  updates, the steps after it (here the fight's spawns) land on earlier world updates and the world takes another
  course. The skill is no unset value either: +-3.4e38 are a resolved hit's and miss's marks (combat.cpp, kHitMark /
  kMissMark), so a sign follows the roll. In WSL on lavapipe render_stats is the same as its golden with all 32 CPUs
  (62 s) and pinned to 4 (`taskset -c 0-3` on run_all, 106 s); pinned to 2 it is cut like the runner's (a frames step 18
  frames short after 16 s, then its 120 s limit) and differs from the cut on; pinned to 2 with both limits raised in
  the clone only (frames 10 min, the scenario 20 min) it is the same again in 173 s. It stays in `LINUX_LEFT_OUT`
  (CI runs it under EI_TIME_SCALE): the runner draws too slowly for its 120 s, and alone on 32 CPUs it is just over 60 s.
- The time zone. A save's info.sav keeps mktime of the harness's fixed date (2001-03-26 12:00), so it followed the
  machine's zone: the build machine's Europe/Prague (summer time that day, UTC+2) gave the goldens' 985600800, GitHub's
  runner (UTC) 985608000 in every save (H51). The harness sets the process's zone to a fixed UTC+2 (`TZ=EET-2`) with
  the fixed date (harness_clock.cpp), so it is the same in any zone; checked on the stub set in WSL under `TZ=UTC
  LC_ALL=C.UTF-8`, the runner's settings.

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

The driver's manifest is `~/mesa-dzn/share/vulkan/icd.d/dzn_icd.x86_64.json`; `VK_DRIVER_FILES` set to it puts any
program on dzn (`vulkaninfo --summary`: "Microsoft Direct3D12 (NVIDIA GeForce RTX 4070 Ti SUPER)", Vulkan 1.2, driver
Dozen; every program prints "dzn is not a conformant Vulkan implementation, testing use only"). The gallium d3d12
driver (OpenGL on D3D12) is not built: the game draws through SDL_GPU's Vulkan only.

Under WSL run_all points the games at it by itself when that manifest exists (`vulkan_driver()`); `--lavapipe` or
`EI_VK_ICD=system` keeps the system's driver, `--dzn` or `EI_VK_ICD=dzn` asks for dzn anywhere on Linux, and
`EI_VK_ICD=<manifest>` names any driver. GitHub's runner is no WSL: CI stays on lavapipe, its reference. Those games
draw on Windows' GPU, so under WSL run_all then also holds Windows' machine slots for them (`hold_windows_slots()`:
Windows' python.exe through WSL's interop runs run_all.py `--hold-slots N [--hold-most M]`, which takes lock files in
Windows' temp folder, the same as Windows' suites and work.py's scheduler, until the run ends), so Windows' and WSL's
games together stay within Windows' 16, and runs no more games at once than it holds. With `--jobs N` it waits for N
slots (2N with a pair in the run), fixed to the run's end; without, it waits for 2 (a pair's) and takes every other one
free at that moment up to `WSL_GPU_SLOTS`, 8 (`EI_HARNESS_SLOTS` sets the most instead): half the machine, so Windows' suites
keep the other half however long the WSL run lasts. Then the hold follows the run's need (H50a1: `follow_need()`, every
2 s): the games it still has to run (running and queued, a pair's two) are the slots it wants, at most the most. More than
it holds: it sends `want <n>` on the Windows process's stdin, which takes the free slots one at a time without waiting
(`adjust_hold()`) and answers `held <m>`, and the games' limit (`GameSlots`, in place of run_all's own slot pool) grows to
m; so a run started while Windows' 16 are busy begins with 2 and grows as they free. Fewer: the limit drops to the games
running (never below), then `want` gives the rest back, so the last games of a run do not hold slots they no longer
use. Checked in `tools/test_work_sched.py` (hold_slots, adjust_hold, GameSlots, rebalance, follow_need).
(Windows' Python started from WSL works in a `\\wsl.localhost` folder, where `/proc/version` is WSL's: `under_wsl()`
asks Linux first, or that side took WSL's pool of 6 as the most.)

Checked (2026-11-04, the machine of "The harness" above, RTX 4070 Ti SUPER):
- The port checks under xvfb-run on dzn: port_render 21 checks, port_shaders 24 programs / 31 pipelines, port_gpu 132
  checks, all passing. SDL_GPU lacks nothing it uses: 4 samples at most (8 asked gives 4, as on lavapipe), BC
  textures, anisotropy, the three present modes (vsync, immediate, mailbox); no logicOp or wide lines, which SDL_GPU
  does not use.
- The image suite less LINUX_SKIP and the four slow ones (139 runs, as above): the same goldens on dzn as on lavapipe
  (133 the same, 6 without a golden). With 4 jobs: lavapipe 427 s (camera_drag_rate cut at its 120 s limit, as
  before) and 607 s beside a Windows suite; dzn 138 to 209 s. With 6 jobs dzn 91 to 112 s. Summed game time over the
  139 runs: 2117 s on lavapipe, 810 s on dzn, the longest dzn run 10 s (camera_drag_rate). The Rebirth set less the
  pairs (19 runs, 4 jobs): lavapipe 115 s, dzn 19 to 32 s, all the same.
- What LINUX_SKIP and the slow list leave out, on dzn: camera_smooth, camera_spin, render_stats, zone_gz15h_tornado,
  the seven pairs (net_pair_master's golden differs as on lavapipe), render_smooth, render_terrain_seams and
  ui_quicksave pass (net_second_master and render_present_window failed on both drivers until H47b: the list above).
  Only net_pair_master is run_all's `LINUX_LEFT_OUT` on dzn.

After H47b (2026-11-04, dzn, 8 slots): `run_all.py --golden` 155 runs in 114 s, all passing, 149 the same, 6 without a
golden (every scenario but net_pair_master); the Rebirth set 102 of 102 the same, in 126 s. On lavapipe (real data and
the stub set) net_second_master, render_present_window and ui_quicksave pass; render_terrain_seams is the one left out
(render_smooth too until H47b1).

Two faults showed only on dzn, both fixed:
- Fences. The renderer kept every submission's fence while the frame timer was off; dzn refused new ones after a few
  seconds of several games (`vkCreateFence` VK_ERROR_OUT_OF_DEVICE_MEMORY, then an abort). Now each goes back once
  its submission has finished (renderer.md, "Fences"; gpu_check checks it).
- libd3d12 unloaded under its own thread. SDL_GPU's driver probe makes a Vulkan instance and ends it before the real
  one; dzn's instance end closed libd3d12, which took libd3d12core with it while one of its threads still ran: a
  SIGSEGV at an address in no module, in a thread other than the main one, before "gpu: vulkan device" (7 of 18 runs
  of ui_widescreen, _dialogue and _hud; preloading libd3d12.so instead crashes every program at its start). The local
  change in the build above keeps it loaded, as dzn already keeps libdxcore: 18 of 18 then.

With both: the image suite twice and the Rebirth set and the left-out set once each, with 4 jobs, Windows' slots held,
no failure and no retry but net_second_master and render_present_window (93 and 128 s for the image suite, 41 s for
the Rebirth set). So it became the local default under WSL (H50a).

The defaults (H50a, 2026-11-04, a fresh clone and harness build in WSL's home): `run_all.py --golden` with no names,
the image suite less net_pair_master, net_second_master and render_present_window (153 runs: the pairs and the slow
ones included), every time all passing, 147 goldens the same, 6 without one:
- 6 slots (Windows' side still capped at 6, fixed since), Windows idle: 208 s, summed game time 1133 s.
- 8 slots beside 8 Windows games (other workers' suites, which kept their 8 throughout): 270 s, summed 2088 s, the
  longest net_pair_quest 165 s; settings_save passed on its retry.
- 2 slots (all 16 busy at the start; the run keeps what it got): 347 s, summed 665 s.
The GPU shared with Windows' games costs more than the jobs gain: the same runs take three times as long beside 8
Windows games. WSL's memory: a game's own RSS is 150 to 250 MB, but dzn's GPU memory counts in WSL's "used" too, about
1.2 GB a game: 8 games peaked at 12 GB of WSL's 16, so 8 is also the most that fits.

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
  format, levels) and the .mmp's extra block; the pixels 0. Kept whole (`KEPT` in stub_data.py, by entry name, H51b):
  the few whose pixels a frame check reads and which fail on blanks: the cursor pictures (`cursor_*`, `sm_cursor_*`:
  render_cursor_present wants opaque and clear pixels and a shape that changes with the gamma), the faces
  (`*mouth*`: render_blink's open and closed rectangles) and bz1g's ground `basegipat000` and `001`
  (render_shadows_outside: the gate's shadow on lit ground). Which ground: each of the eight basegipat textures painted one colour in
  the stub set, the pixels the shadow changes counted by colour: 000 and 001 take 85% of them; the check then sees
  2.18% (vanilla; the install 2.33%, allowed 0.3 to 5%). The kept textures are 2.3 MB of the vanilla set
  and 7.5 MB of Rebirth's (its ground is 2048 x 2048). A new frame check that fails on blanks adds its textures here
  the same way: a list, not a pattern (a pattern on every texture would move every other frame check's pixels and
  thresholds; the list leaves them as they were checked).
  render_smooth (H51c): its msaa and anisotropy checks want pixels that change with the filter; blank textures gave
  0.03% against at least 0.2% (anisotropy 8: 0.000% against 0.5%). Kept, as the models in view at zone1's start use
  them (found by logging the texture each mesh draw bound and joining it to the texture's name): `dgunbirdruins00`,
  `dguntorchruins01`, `ruins00` and `tree02`. Then the stub set gives what the install does (smooth / msaa 2 /
  anisotropy 8: vanilla 2.95 / 0.61 / 2.30% on the stubs against 3.03 / - / 2.31% on the install, Rebirth 1.41 / 0.47 /
  0.90% against 1.41 / - / 0.91%). The ruins alone leave Rebirth's anisotropy check at 0.47% (its tree is 2048 x 2048
  DXT5, 5.4 MB of the set), the tree alone leaves msaa 2 at 0.13%. The units' skins are composed textures (made from
  the game's pieces) and cannot be kept (their pieces can, below); the ground needs no pixels for this scenario.
  render_shadows_decals and render_weather_panel (C159, both sets since H62e; Rebirth's copies add 23 MB of pixels):
  found 0.0000% in CI (run 41953378949) with the ground prints and the HUD figure drawn
  blank: `footprints*`, `bloodprints`, `fireprints` (clear on blank pixels), zone2's ground pages `zone2000`-`2007`
  (gz2g; printlight compares a print's brightness with the ground's under it: black ground gave ratios 1.4 to 2.3
  against 1.0 to 1.13) and the thirteen layers the hero's skin is composed of (`unhuma*`; a wet black figure is as
  black as a dry one). Not a driver gap: the install passes on lavapipe and dzn, the old stub set fails on both, the
  new one passes on both. A layer's name: log `FindLayers`' `found` (texture_load.cpp). H62e: C159 kept them in the
  vanilla set only, and the Windows CI runs 42016421240 and 42039247533 failed render_shadows_decals on Rebirth's
  (diffcapture decals bare 0.0000%, printlight no pixel differs). Keeping the names in Rebirth's set was not enough:
  its textures.res stores some names twice over one range (`Zone2000` and `bz2g000`, `Zone2003` / `bz2g003`, `Zone2005` /
  `bz2g005`: one entry's bytes, two names), and blanking the other name zeroed the kept pixels, so the ground of gz2g
  stayed a checkerboard of lit and black pages. `stub_in_place` now keeps every name of a kept entry's range. With it
  Rebirth's printlight reads as the install's (decals 66 69 61 over 38 85 59, ratio 1.071; before 57 54 59 over 20 56 56,
  1.286 against at most 1.13), and render_water, render_smooth, render_shadows_outside, render_weather, _roofs and _panel
  pass on that set; the vanilla set is the same. Check after a repack: the kept entries' bytes equal the install's.
  render_water (H51d): its diffcapture of the beach (the ripples moving between two frozen moments, at least 1.0% of the
  pixels) reached 0.9907% on GitHub's runner with the vanilla stubs; the water layer draws the same page textures as the
  ground (Terrain::DrawWater binds `pageTextures` again), so black pages leave the sand and the water over it dark and the
  ripples' shading changes small. Kept (vanilla only): zone1's three pages `zone1000`-`zone1002` (512 x 512, 0.5 MB;
  zone10's start with `zone10`, the pattern `zone100[0-2]` leaves them out). WSL on lavapipe, 4 cores: the old stubs 2.26%,
  with the pages 3.79%, the install 4.21%; Rebirth's old stubs 2.47% (no failure in CI), with `basegipat005` (below) 3.26%.
  The runner's lavapipe gave 0.99 where WSL's gives 2.26, so the margin on the runner is the open question: the kept
  pages bring the stubs to 90% of the install's figure, 1.8 times what they gave.
  The weather scenarios (H60), found by running them on the stub set in WSL on lavapipe: `rainsnow.mmp` (both installs,
  KEPT: the rain and snow particles' texture, 21 KB and 87 KB; blank, the particles are clear and render_weather's
  weatherparticles on / off frames are the same, 0.0000% against at least 0.1%) and `basegipat005.mmp` (both, KEPT, 0.2 MB
  and Rebirth's 2.8 MB: the ground of bz1g under the gateway's floor; render_weather_roofs' rain and snow captures
  differed in 72.7% of the rectangle against at least 80% on blank ground, 95.5% and 97.4% with the page: each of
  `basegipat00x` painted in turn, 002 and 003 together 74.7%, 004, 006 and 007 nothing). The pack: 85.6 MB (79.3 before),
  both installs, level 22. After H62e (Rebirth's prints, ground pages and skins, and the aliases above): 93.0 MB
  (97.6 million bytes, under GitHub's 100 MB file limit: the next addition to Rebirth's set may need the split parts
  the CI job unpacks, `ei-stub-data.tar.zst.00`, `.01`).
- Sounds (.wav): every chunk; the samples 0. A voice ends by its data's bytes over its byte rate (sound.md).
- MP3 (speech, music): every frame header (the game counts frames: ScanMp3), the frames' data 0, an info frame whole.
- Movies (.bik): header and frame index (frame count, size, rate, keyframes); every frame emptied (no sound packet, no
  video packet: the decoder keeps the frame before). The game reads a movie's frame numbers, never its pixels.
- Programs (.exe, .dll, .asi): empty files, for the profile's presence check.

Checked (2026-11-03, the read-traced install runs against the stub runs): on Windows the image suite on the stub set
149 the same / 5 without a golden and the Rebirth suite 28 the same, as on the installs. Four scenarios failed their
own pixel checks on blank textures, their goldens the same: render_blink, render_cursor_present, render_shadows_outside,
render_smooth; on Linux (WSL's lavapipe, GitHub's runner) the first three the same way (open and closed faces alike, the
gate's shadow on black ground, a cursor of 1024 transparent pixels) and they passed on the install copies (H51): the
data, not the renderer. With `KEPT` (2026-11-04, H51b) the three pass on both stub sets, and both suites on the
stub set are the same as on the installs: image 195 the same / 8 without a golden, Rebirth 111 / 1 (306 / 9 together),
render_smooth the one scenario that failed its own check. With H51c's textures (above) it passes too, and the suites on
the stub set (2026-11-04): image 195 the same / 14 without a golden, Rebirth 111 / 4, every run passing but render_water
(new since R17a: its diffcapture of the shore reached 0.989% on the stubs against at least 1.0%; it passes on the
install; kept zone1 pages, H51d). `stub_data.py check <stub> <install> <runs>` confirms every file the runs read is in the stub set at
the install's size (the movies aside). In WSL (the packed set unpacked as the CI job does)
each run on the stub set against the same run on the install copies, digest for digest (golden.py diff): the image
suite less the pairs 142 of 142 comparable runs the same (4 of them only once rerun on a quiet machine: under load
llvmpipe's frame waits run out), camera_smooth and camera_spin cut by their 120 s limit on both, the watchdog
scenarios without a digest; the Rebirth set less the pairs 22 of 22. Against the Windows goldens every Linux run
differed from record 0 (the main menu's animals), on the install copies as on the stubs: the systems' float maths
functions round differently (float-maths.md, "Maths functions"). With lib/maths.h boot plays on Linux as on Windows,
digest 1 record for record, and the suites on Linux are the same as the goldens ("The harness"). Over 60 s on
llvmpipe besides the pairs: camera_smooth, camera_spin, render_stats, zone_gz15h_tornado.

Sizes: the vanilla set 724 MB on disk (121 MB of it not zero: figures.res 53, the maps 59), Rebirth's 1850 MB (240 MB
not zero): its high-resolution textures.res is 655 MB of blanked pixels and the LostInAstral mod 711 MB (its speech and
sfx blanked; 119 MB of maps and figures that no run reads). The blanks cost next to nothing packed; truncating them
would change the archives' layout and only save the runner's disk. `stub_data.py pack` (Python 3.14's zstd) puts both
in one .tar.zst of 93.0 MB (79.3 MB before H60 and H62e; 74.5 MB before `KEPT`, 77.1 before H51c): level 22 with a 128 MB window and long matching, the two installs' copies of a file next
to each other. Against xz -9 with a 256 MB window: 65.0 MB, but 7 to 9 s to unpack against zstd's 1 s; zstd -19
--long=27 76.3 MB. Leaving out LostInAstral would give 49 MB. Building takes about 20 s an install, packing 5 minutes.

### The CI job

`.github/workflows/suites-linux.yml` ("Linux") has two parts. Its build job builds the game and the harness with
warnings as errors and runs ctest (above), with no game data; it saves its compiler cache under the commit. Then eight
suites jobs in parallel each restore that cache (the harness build is all hits and a link), check out the private
repository with a read-only token, unpack the stub set into the runner's temp folder, remove the checkout and play
every eighth scenario of the sorted list (both suites, one game at a time) under xvfb on llvmpipe, less every render_*, net_* and
camera_* scenario (user, 2026-11-06: the renderer's looks, the two-game runs and the camera runs were most of the time and the
failures; named in the `scenarios` input they still run) and `LINUX_SKIP`;
a job fails when a golden differs. It runs on pushes to main and `claude/**` (not docs-only ones), on pull requests
from this repository's own branches (a fork's gets no secrets and the suites are skipped) and by hand; it uploads
nothing but the build job's two shader files, and the data is outside every cached path. The suites are off until
the variable below is set.
A third part, the gcc-latest job, compiles the game and the harness (no checks, warnings not errors) in the unpinned
`gcc:latest` image with the build job's shaders (EI_PREBUILT_SHADERS): every gcc release drops some of libstdc++'s
indirect includes, so a file that uses a name without its header builds on gcc 13, Apple clang and MSVC and breaks on
the newest gcc (pull request 1: gcc 16, std::find_if without `<algorithm>`). Include the header of every std name a
file uses. A local check on macOS: configure with `-DCMAKE_CXX_FLAGS=-D_LIBCPP_REMOVE_TRANSITIVE_INCLUDES`, which
drops libc++'s compatibility includes (it caught std::ranges::find without `<algorithm>` in four files, not the
pull request's file: libc++'s and libstdc++'s leftover includes differ).
The Windows twin (suites-windows.yml, by hand: windows-latest on D3D12 WARP) is a smoke job like the macOS one; harness.md, "The Windows CI job".

`EI_TIME_SCALE` (3 in the workflow; default 1, never below 1) multiplies the harness's real-time limits: a `frames` step's
(10 s plus 100 ms a frame), the real-time safety limit of the other waits (four times the wait plus 10 s) and the
scenario's (60 s or its `timeout`). The game reads it (harness.cpp, `TimeScale`) and so does run_all.py (`time_scale`,
for the limit it reports and enforces); nothing that counts the world's clock changes, so a run that is not cut is the
same at any scale. The reason (H58, CI run 113628906828, 2026-11-04): 14 failures over 8 shards, none a Linux
difference, all real-time limits of a slow runner (a cut frames step: render_preview_blend, render_cursor_present,
render_weather_units, net_host_death, terrain_water_waves in Rebirth; the 60 s limit: render_water_looks, render_weather*,
unit_ai_corpse, party_leader_switch). A cut frames step also changes the digest after it (H56: the later steps land on
earlier world updates). The same 202 scenarios took 9% longer than in the run before, and some 40 to 90% longer
(world_load 6 s to 11 s), so the runner's speed varies and adding each victim to `LINUX_SKIP` never converged.

Since 2026-11-06 the workflow's `LINUX_SKIP` is only ui_minimap_edges, the three ui_widescreen ones, unit_ai_bridge and
zone_gz15h_tornado_client, plus ui_widescreen_screens (timed out on Rebirth, run 42155765669): the render_*, net_*, camera_* and
mod_lost_in_astral_* scenarios are left out by name pattern instead (the Lost in Astral runs hit their 180 s limits on
llvmpipe one after another: cz_ingos in 42155172606, start in 42199781767). What follows is
the history of the longer list (run_all's `LINUX_LEFT_OUT` for local runs is unchanged).

`LINUX_SKIP` held what cannot match on llvmpipe for reasons other than speed: every two-game scenario (net_pair_*,
net_zone_leader, net_zone_vars: their frames stall past the harness's 16 s frame wait), render_terrain_seams (its level
steps differ on lavapipe) and render_smooth (llvmpipe gives 4 samples whatever is asked). Until H58 it also held the
slow ones, which were taken out when the scale came: camera_drag_rate, camera_smooth, camera_spin, camera_drawn,
render_drawn_lights, render_drawn_motion, render_shadows_outside, render_stats, spells_more, levers_key, ui_minimap_edges,
the five ui_widescreen ones, unit_ai_bridge, zone_gz15h_tornado and zone_gz15h_tornado_client. Run 41953378949 (scale 3)
cut the weather scenarios, the drawn-frame ones, Rebirth's widescreen set, unit_ai_bridge, the tornado client and
camera_smooth, which are back in the list; render_shadows_decals and render_weather_panel are out of it again (C159,
"The stub data": blank ground prints, ground and hero skin, not the driver; they run on the repacked stub set). The four
render_weather* scenarios (not _panel) are out of it too (H60), each with a `timeout` line the scale multiplies, set from
WSL on lavapipe with `taskset -c 0-3` and the factor of two the runner seems to be slower by (camera_spin: 207 s here, cut
at its 360 s, step 31 of 51, twice on CI's runner in run 41971981364 and through the next run, 41972714191):

| scenario | virtual waits | WSL, 4 cores (vanilla) | Rebirth, 4 games at once | `timeout` x 3 |
| --- | --- | --- | --- | --- |
| render_weather_shelter | 93 s | 127 s | 274 s | 150 x 3 = 450 s |
| render_weather_roofs | 180 s | 218 s | 303 s | 200 x 3 = 600 s |
| render_weather | 618 s | 586 s | 1071 s | 550 x 3 = 1650 s |
| render_weather_units (3840x2009, msaa 8) | 188 s | 755 s | 1098 s | 650 x 3 = 1950 s |
| camera_spin (1480 frames) | | 207 s | | 200 x 3 = 600 s |

(The Rebirth column ran four games at once on four cores each, which costs 1.5 to 2 times: a bound, not a comparison.) The
first four sit in four different shards (the names sorted, every eighth); camera_spin shares shard 7 with the units one,
about 40 minutes of the runner's time between them. The retry a failed run gets (run_all) doubles a cut run's time.
A separate job would only move them; the shard job's 90 minutes are the limit to watch.

Why the weather scenarios are slow (C159, WSL on lavapipe pinned to 4 cores with `taskset -c 0-3` as the runner has:
render_weather_roofs stopped at step 53 of 72 against the runner's 51, so it is the same). A virtual `wait` costs real
time because every frame in it is drawn: 20 s of virtual rain 14 to 17 s of wall time at 1440x1080, 960x720 and 640x480
alike (so not fill-bound), and `weatherparticles off` changed nothing. A frame: 0.16 s clear and 0.34 s rain at 1440x1080;
at 640x480 0.22 s clear and 0.23 s rain (a CPU floor of about 0.2 s: the game's updates, vertex work and uploads on
4 cores; the machine was shared, so the figures are good to 30%). Rain's added cost is the particles' fill, which a
smaller window removes, the floor stays. So a smaller CI resolution does not bring them in: render_weather_roofs (9 x 20 s
of waits) would go from about 185 s to about 150 s against its 180 s, render_weather has 618 s of waits (about 430 s),
render_weather_units 188 s and shelter 93 s, and the scenarios fix 1440x1080 and pixel rectangles in their
diffcaptures. They need a `timeout` line of their own (the scale multiplies it) or a job of their own, not a size. Before the split (2026-11-04) one job played the image suite in
83 minutes and the Rebirth set in 39, and the slow scenarios made all its 18 failures (14 timeouts, 4 pair frame
stalls). Local Linux runs without names leave out `LINUX_LEFT_OUT` (run_all.py) instead; `EI_TIME_SCALE=3` on a local
run takes them as well, as far as the machine has the cores.

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
- The keys (LX4; src/platform/posix/keyboard.cpp, the same on macOS): there are no Windows layouts to give the virtual-key codes,
  so `LayoutKey` takes them from what the key types in SDL's keymap (`SDL_GetKeyFromScancode`): on a layout whose
  letter keys type Latin letters a letter's or digit's code and the US punctuation characters' codes
  (`sdl_keys::KeyOfTyped`: AZERTY's M key VK_M, its comma key VK_OEM_COMMA), on another (Russian, Greek) the US codes,
  as Windows' Russian layout has them (SDL's event key codes are the US layout's there). That is Windows' code for
  every key on us and ru; Windows' French and German layouts give some punctuation keys other VK_OEM codes, and
  German's ß and - keys share VK_OEM_MINUS here (no game code reads the VK_OEM codes: bindings and the console go by
  scan code). Ctrl: a letter key its control character, where the layout's key types `[`, `\` or `]` ESC, FS or GS,
  else none (Russian's х and ъ keys none, as on Windows). Key names as Windows' GetKeyNameTextW gives them: a letter
  key its code's letter (Russian's Й key "Q", as on Windows), another moved key what it types (х, ж, ё; French's
  digit row & é "), the rest Windows' US names ("Num 7", "Right Ctrl"): neither SDL nor xkb has key names in the
  layout's language. A dead key types no character in SDL's keymap and keeps the US key's name (French ^ is "[").
- Not played with real data yet: menus, maps, movies, sound, saves and multiplayer are unchecked on Linux.
- Frame pacing under WSLg (X11 through XWayland, GALLIUM_DRIVER=d3d12, VSync on; PL20, 2026-11-03): 64 to 85 frames a
  second, 10 to 28 ms apart, where Windows' 60 Hz screen gives 16 / 17 ms; on a busy machine 32 to 38, 16 to 66 ms
  apart. XTest pointer moves at 125 Hz (4 counts) arrive as 4, 8 or 12 counts a frame at 70 a second and 8 to 40 at 35:
  the beat between the mouse's reports and the frames, and the frame's length. The camera's mouse turn took each frame's
  motion as its rate, so it ran with both; it now takes the mouse's speed over a 60 Hz frame's window (ui.md, "Camera
  mouse turn"): the same moves give a drag of 5 to 14 (8.3 is the speed), 1000 Hz moves of 1 count 16 to 22 (16.7). The SDL build in WSL has no Wayland backend (no
  wayland-client development files), so SDL_VIDEO_DRIVER=wayland was not measured.
