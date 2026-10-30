# Native macOS port

The `macos-port` branch builds a native Apple Silicon application with Apple Clang, SDL3 and sokol/Metal. It has
been run against the Russian game data: the animated menu, sound, new-game dialog and first campaign map work.
This is an initial port, without the Windows scenario harness or cross-platform golden verification.

## Build

Install Xcode's command-line tools and the shader tools, then obtain the official sokol shader compiler. The
following pins the compiler revision used for this port; the binary and all generated shaders stay in `build/`.

```sh
brew install cmake ninja
mkdir -p build/tools
curl -L https://raw.githubusercontent.com/floooh/sokol-tools-bin/b975d5642093d62e76a1e088bdae945d6d8b8ff2/bin/osx_arm64/sokol-shdc -o build/tools/sokol-shdc
chmod +x build/tools/sokol-shdc
cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/macos -j 8
```

The result is `build/macos/ei-rebuild.app/Contents/MacOS/ei-rebuild`. For smaller compile checkpoints:

```sh
cmake -S . -B build/core -G Ninja -DEI_CORE_ONLY=ON
cmake --build build/core -j 8
cmake -S . -B build/port -G Ninja -DEI_PORT_CHECK_ONLY=ON
cmake --build build/port -j 8
ctest --test-dir build/port --output-on-failure
```

`EI_PORT_CHECK_ONLY` builds the game library plus SDL/POSIX without the renderer. It is also the starting point for
Linux; a runnable GL backend is still needed there. Neither Linux nor clang-cl on Windows was tested in this session.

## Run and check

For normal play, run from the repository:

```sh
python3 tools/run_macos.py --play
```

This runs until you quit and reuses `run/macos/` for saves, settings, copied `Config` and logs. The data defaults to
`~/workspace/ei`; pass `--data` to choose it on the first run. Edit `run/macos/ei_settings.ini` to change resolution
or other settings; subsequent launches preserve it. Startup movies are skipped unless `--intro` is supplied.
`[video] FullScreen = false` uses a centred window with the normal macOS title bar and window controls;
`FullScreen = true` uses a borderless window covering the monitor.

For bounded checks:

```sh
python3 tools/run_macos.py --data ~/workspace/ei --check
python3 tools/run_macos.py --data ~/workspace/ei --seconds 60
ctest --test-dir build/macos --output-on-failure
```

The launcher creates a new folder under `build/macos/runs/`, links read-only game asset folders, copies `Config`
and settings, and redirects all writable game paths there. It runs windowed, skips the startup movies unless
`--intro` is passed, and stops the process at the deadline (exit 124 means the deadline was reached). It retains
`stdout.txt`, `ei-rebuild.log`, settings and other output. Do not recursively delete these scratch trees while they
contain links to the installed game data.

The native executable accepts `EI_GAME_ROOT` for its working/data folder and `EI_SETTINGS_PATH` to pin its settings.
Launching the app bundle directly without those variables expects data beside the executable. Prefer the scratch
launcher while testing, so the installed data is left untouched.

## Implementation and checks

- All 383 game/library translation units compile as native ARM64 C++20. Game code uses ordinary C++ functions,
  inline helpers and thread callbacks; compiler/OS selection stays in `src/platform`. Portable math keeps
  integer-conversion sentinels and disables multiply/add contraction.
- The POSIX backend implements files, mappings, INI reads/writes, sockets, process locking and user-data paths.
  File lookup normalizes backslashes and folds case a component at a time, so `res\\database.res` opens
  `Res/database.res` on a case-sensitive volume. Exact spelling wins; ambiguous folded matches sort consistently.
- Shader sources are annotated GLSL in `src/render/shaders`, collected by `shaders.glsl`. A shared CMake rule
  invokes sokol-shdc directly for HLSL5, Metal and GL 4.3, producing `shaders/shaders.h` in the build tree.
  There are 22 programs; multisample depth uses one shader at every sample count. Generated descriptors own
  bindings and reflection, and generated C++ structs supply the uniform layouts. No external glslang,
  SPIRV-Cross or fxc tool is needed. See docs/notes/renderer.md, "Shader authoring".
- All render sources are shared across backends, including presentation, cursors, readback and shader selection.
  Native operations live in `platform/graphics.h` with one implementation per backend (`graphics_metal.mm` or
  `win32/graphics_win32.cpp`). Texture copies, readback and mip generation use the native API where this sokol
  revision has no equivalent API. Metal shares sokol's command queue and flushes only pending command buffers;
  empty commits otherwise rotate uniform storage without a fence and corrupt overlapping glyph draws.
- Retina windows request high pixel density, and the game sees pixels only: platform_sdl.cpp makes the window
  VideoResolution / the display's content scale in points (1280x800 points for 2560x1600 on a 2x display), so its
  drawable holds the configured resolution, and scales mouse positions, motion, ScreenToClient / ClientToScreen and
  the cursor position by the window's pixel density. Nothing is written back to the settings. Native 2x is
  VideoResolution 2560x1600.
- Apple's libc++ has no float `std::from_chars`: settings_document.cpp's `ParseFloat` reads with `strtof` (the C
  locale) where the library does not define `__cpp_lib_to_chars`, refusing what from_chars refuses.
- `port_platform` covers path folding, file/mapping behavior, INI updates, single-instance exclusion, UDP,
  math edge cases, thread callbacks, caller capture and the existing 20,000+ settings checks. `port_render` runs
  the same 29 glyph cases as the Windows canvas checker, against its CPU pixel oracle: Cyrillic, shadows, clipping,
  overlap, batching, stream overflow and 555/565/888 destinations. It also checks partial uploads, region copies and
  readback bounds. All pass, with zero differing glyph pixels. Configure with `-DEI_RENDER_CHECKS=ON` to include
  diagnostic readback checks, verify generated mips, simulate device loss, restore canvas history, and repeat the
  glyph oracle after recovery. Readback also follows sokol's active dynamic-image slot across commits.
  The renderer checks also compile all 22 Metal programs, compare cloud pixels against saved HLSL hashes,
  and verify canvas images, copies, greyscale, RGB565 packing and MSAA colour/depth copies at each supported
  sample count. On this Mac the diagnostic run has 110 checks (2x and 4x MSAA). Normal builds leave the
  diagnostic readbacks off.

## Remaining verification and features

The SDL scenario
harness and Linux GL runtime remain unfinished; no claim of simulation equivalence across compilers follows from
these native checks. Fullscreen, resizing across displays, Intel Macs, multiplayer, long play sessions and movie
playback still need coverage. The native cursor is presented with normal frames; optional between-frame cursor
presents and GPU timing queries are not implemented on Metal. The scratch launcher disables the independent cursor.
