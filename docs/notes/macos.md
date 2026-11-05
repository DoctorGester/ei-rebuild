# macOS

The game builds natively on Apple Silicon with Apple Clang from the same CMakeLists.txt and `src/main.cpp` as on
Windows and Linux: SDL3 for the window, input, sound and threads, POSIX for the rest of the platform
(src/platform/posix, as on Linux), and the renderer on SDL_GPU's Metal driver (src/render/gpu.cpp; renderer.md
section 15c), its programs' MSL from the HLSL (renderer.md, "Shader authoring"). The user's macos-port branch ran it
against the Russian game data on sokol's Metal backend (the animated menu, sound, the new-game dialog and the first
campaign map); since MAC4 / R43h the Mac draws through SDL_GPU like the other systems, and nothing of the branch's
Metal backend (graphics_metal.mm) is left. macOS cannot be built on the Windows machine: CMake's Apple parts are kept
right by reading, and the Mac's own checks are below ("Checks on the Mac"). The scenario harness builds and plays
the goldens (below, "The harness").

## Build

Xcode's command-line tools, CMake, Ninja, Python 3, and the LunarG Vulkan SDK for macOS for DXC (Microsoft ships no
macOS DXC; the SDK has `lib/libdxcompiler.dylib` and `include/dxc/dxcapi.h` and sets `VULKAN_SDK`):

```sh
brew install cmake ninja
# the Vulkan SDK for macOS from https://vulkan.lunarg.com (its setup-env.sh sets VULKAN_SDK)
cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo   # or -DEI_DXC_ROOT=<folder> without VULKAN_SDK
cmake --build build/macos
```

The configure step builds the shader toolchain once (shadercross and spirv-cross against that DXC;
tools/shader_toolchain), as on the other systems. The result is `build/macos/ei-rebuild.app/Contents/MacOS/ei-rebuild`
(CMakeLists.txt makes ei-rebuild a bundle on Apple).

## Release

release.yml's `macos` job (beside Windows and Linux; Actions > Release > Run workflow) builds on GitHub's arm64
`macos-15` runner: pip's cmake 3.31 and ninja, DXC copied out of the LunarG Vulkan SDK (`VULKAN_SDK_VERSION`,
installed with its installer's command line, then only `lib/libdxcompiler*.dylib` and `include/dxc` kept in
`~/dxc-macos`, cached by the version and named by `-DEI_DXC_ROOT`), and the shader toolchain cached as in suites-linux.yml.
The build is Release, universal (`-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`: one configure, SDL3 builds both
slices; the shader toolchain stays the runner's arm64) for macOS 13.3 and later (`-DCMAKE_OSX_DEPLOYMENT_TARGET`;
Apple's libc++ has float `std::to_chars`, which settings_document.cpp and std::format's floats use, from 13.3). Then
ctest's four checks, the x86_64 slice's `ei_port_check` under Rosetta where the runner has it, a check that the
executable has both slices and links only system libraries, the release's version into Info.plist, an ad-hoc
signature (`codesign -s -`: arm64 code must carry one) and `ei-rebuild-macos.zip` (ditto) with the app, a README.txt
and third_party_notices.txt.

The app is not Developer ID signed or notarised (that needs an Apple Developer account: the user's call), so a
downloaded copy is quarantined and Gatekeeper refuses it: `xattr -dr com.apple.quarantine ei-rebuild.app` once, or
Control-click > Open (macOS 15 and later: open it once, then System Settings > Privacy & Security > Open Anyway).
The zip's README.txt says how the app finds the game folder and where it writes ("The game folder and the log" below).

Not run yet (2026-11-04): the job as a whole. Its weakest guesses are the Vulkan SDK's download name and installer
command line, ctest's GPU checks on the runner's virtual Metal device, and a universal Release build at 13.3 (the
Mac builds so far were the user's arm64 RelWithDebInfo at the SDK's own target).

## Run

For normal play, from the repository:

```sh
python3 tools/run_macos.py --play
```

It runs until you quit and keeps `run/macos/` for saves, settings, a copied `Config` and the logs. The data defaults to
`~/workspace/ei`; `--data` chooses it on the first run. `run/macos/ei_settings.ini` keeps the resolution and the other
settings across launches. The start movies are left out unless `--intro` is given. `[video] FullScreen = false` is a
centred window with the macOS title bar and controls; `true` SDL's desktop full screen, a Space of its own as the
green button makes (platform_sdl.cpp): the menu bar and the Dock hidden, on a display with a notch the part below it
(1728x1080 points of 1728x1117 on a 14" MacBook Pro). A borderless window over the display's bounds, as on Windows,
stays under the menu bar and the Dock there.

Bounded runs (at most 60 s; exit 124 when the time ran out):

```sh
python3 tools/run_macos.py --data ~/workspace/ei --seconds 60
```

Each makes a new folder under `build/macos/runs/`, links the game data's read-only folders (Res, Maps, Movies, Stream,
Camera) into it, copies `Config` and the settings, and points every writable path there (`EI_GAME_ROOT`, `EI_OUT_DIR`,
`EI_SETTINGS_PATH`); it keeps `stdout.txt`, `ei-rebuild.log` and the settings. Do not recursive-delete such a folder
while it holds the links.

## The game folder and the log

The app started without `EI_GAME_ROOT` (opened from the Finder, the release's way) takes one rule
(`platform::DefaultGameFolder`, src/platform/posix/paths.cpp): the folder holding `ei-rebuild.app` when it holds the
game's data (`Res/database.res`, its names matched with Windows' case rules), else the folder named in
`~/Library/Application Support/ei-rebuild/game_folder.txt` when it still does, else a folder picker (SDL's
`SDL_ShowFileDialogWithProperties`, NSOpenPanel run modally before the window exists; `SDL_DIALOG` is on for Apple
only, CMakeLists.txt), asking again while the chosen folder lacks the data; its choice is written to that file.
Cancelled, the program ends (main.cpp: no game folder, exit 1). SDL's video runs only around the picker and quits
before `StartWindowSystem` starts it again with the game's hints. A quarantined app runs translocated (from a random
read-only copy), where nothing is beside it: the picker then, once, so beside-the-app works only after `xattr -dr`.

The log and a run's other output go to `~/Library/Logs/ei-rebuild` (`platform::DefaultOutputFolder`; the Console app
lists it) unless `EI_OUT_DIR` names a folder. The settings are `ei_settings.ini` in `~/Library/Application Support/ei-rebuild`,
beside `game_folder.txt` (`platform::DefaultSettingsFolder`, which settings.cpp asks for its first place), not inside
the bundle: the app can be replaced or run translocated without losing them or changing the signed bundle, and the
folder is always writable. One file serves every game folder (its `Work path` is relative). A file from an earlier
build at `~/Library/Application Support/EvilIslands/ei_settings.ini` (settings.cpp's per-user place) is still read when
the new folder has none. `EI_SETTINGS_PATH` names another file as before. Windows and Linux keep the program's own
folder for the game, the log and the settings (the Linux build checks `DefaultSettingsFolder`'s Linux branch and
`PrefFolder`, which the macOS branch shares with `UserDataFolder`; port_check asserts the macOS branch's folder).

Not checked on a Mac yet (written without one, 2026-11-05; compiled only by the Release workflow's macOS job): the
picker's look and its answer, the remembered file, the log's and the settings' folders, and SDL's video quit and
started again.

## What differs on macOS

- **Compiler and system** (src/platform): `-ffp-contract=off` as on Linux; the program's path from
  `_NSGetExecutablePath`; sockets with `sin_len` and `SO_NOSIGPIPE` (no `MSG_NOSIGNAL`); MXCSR is x86 state, so
  render.cpp's float-control check is x86-64 only.
- **Retina**: the window is made with high pixel density and in points (VideoResolution / the display's pixel density:
  1280x800 points for 2560x1600 on a 2x display), so its drawable holds the configured resolution in pixels. The game
  sees pixels only: platform_sdl.cpp scales the mouse's positions and motion, ScreenToClient / ClientToScreen, the
  cursor's position, hold and warp by the window's density (`PixelsPerPoint`, 1 elsewhere), and
  `PrimaryDesktopSize` is the display in pixels. Native 2x is VideoResolution 2560x1600. A cursor shape is made in
  points with the game's pixels as its high-density image.
- **The held cursor** (the camera's right-button turn): SDL's relative mode freezes the pointer itself
  (CGAssociateMouseAndMouseCursorPosition). Two local patches of the vendored SDL's Cocoa backend
  (third_party/SDL/README.txt) make it honour `SDL_HINT_MOUSE_RELATIVE_CURSOR_VISIBLE`: `SDL_cocoamouse.m` neither
  hides the cursor nor warps it to the window's centre when relative mode starts with the hint set, and
  `SDL_cocoawindow.m` keeps the chosen cursor shown in relative mode then. So the cursor stays visible at the press
  point. Taking hold also drops the mouse's motion from before the press (`Cursor::SetHeld`), so it is not the turn's
  first step.
- **Settings**: Apple's libc++ has no float `std::from_chars`; settings_document.cpp's `ParseFloat` reads with
  `strtof` (the C locale) where `__cpp_lib_to_chars` is not defined, refusing what from_chars refuses.
- **Bit-flag operators** (lib/bit_flags.h): the word-and-enum operators are constrained by their return type
  (`enable_if_t`), since Apple Clang checks an operator's parameters before its constraints, and a plain enum would
  have matched them (port_check's "plain enum bit operations").
- **Rendering**: SDL_GPU on Metal (gpu.cpp's driver `metal`, MSL code). The MSL is spirv-cross's with each resource
  at its SPIR-V binding, which gen_gpu_shaders.py checks on every system's build. Metal has no D24S8 (the renderer's
  depth is D32F) and no Mailbox present mode (gpu.h's fallbacks take Vsync for it). SDL 3.4.18's Metal driver took
  a texture upload's rows as packed, not at `pixels_per_row`, so gpu.cpp's 256-byte padded rows went up skewed (the
  movies, port_gpu's read-backs); a local patch of the vendored SDL reads them (third_party/SDL/README.txt), upstream's
  fix being SDL commit 32f9c098d5.

## The harness

`-DEI_HARNESS=ON` builds `ei-rebuild.app` with the harness (tests/verify; its system layer is system_posix.cpp, with
macOS's calls where Linux's differ: `os_sync_wait_on_address` for a pair's shared wake-up, libproc for the network
scan, Mach for the main thread's CPU time and the stack regions, the arm64 registers in a crash report). The harness
finds `config/harness` from the bundle (two levels above the folder holding `ei-rebuild.app`), and run_all.py starts
`ei-rebuild.app/Contents/MacOS/ei-rebuild`. The tools need a recent Python (golden_explain.py's f-strings: 3.12 or
later; `brew install python@3.14`), not Xcode's 3.9:

```sh
cmake -S . -B build/macos-verify -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DEI_HARNESS=ON
cmake --build build/macos-verify
EI_BUILD_DIR=$PWD/build/macos-verify EI_GAME_VANILLA=~/workspace/ei \
    python3.14 tools/run_all.py --vanilla-only --golden --jobs 6        # or scenario names
```

No virtual display: every game opens its window on the desktop. Against the goldens (2026-11-04, M4 Pro, macOS 15.3,
the vanilla install at ~/workspace/ei, a 3456x2234 display): 155 runs in 71 s at 6 at once, 144 the same, 6 without a
golden, and these five differ:
- ui_widescreen_dialogue, ui_widescreen_hud and render_present_fullscreen take the display's size (`resolution
  desktop`, full screen); the goldens are a 3840x2160 desktop's, as on Linux (linux.md, "The harness").
- net_pair_master: the peer lists the server one poll sooner, as on Linux.
- net_second_master failed (a TCP connect to a closed loopback port is refused at once, as on Linux, so the box the
  scenario cancelled was already gone); its Esc is now `ifwindow masterbox key esc` (linux.md, "The harness"; checked
  on Linux, not run again on the Mac).
The Rebirth suite was not run (no Rebirth install on the Mac).

## Checks on the Mac

`ctest --test-dir build/macos --output-on-failure`: `port_platform` (path folding, the Bink container read through
platform::File, files and mappings, the .ini reader, the one-instance lock, UDP, maths, the settings checks),
`port_render` (the glyph cases on Metal, the window's pixel size, the client / screen round trip, the cursor's hold;
the X11 present read-back is Linux's only), `port_shaders` (every program's MSL and its pipelines on Metal) and
`port_gpu` (gpu.cpp on Metal).

Run on the Mac since MAC4 / R43h (2026-11-04, M4 Pro, Vulkan SDK 1.4.363.1): the configure and build, and ctest's four
checks (all pass with the SDL upload patch above). Not yet checked there:
- a play run (`tools/run_macos.py --play`): the menu, a zone, the camera's right-button turn with the cursor visible
  at the press point, the cursor's shapes at 2x, full screen and windowed, a resize, a movie and the loading screen;
- the shadows (the receivers' `shadow_range` is the texture shadercross's MSL had put at slot 0) and the smooth option
  (multisampling with the depth copy).

Not covered yet: Intel Macs, displays of other densities, multiplayer, long sessions; the overlay's GPU time comes
from the fences as on the other drivers.
