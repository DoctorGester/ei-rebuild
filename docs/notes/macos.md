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

The result is `build/macos/ei-rebuild.app/Contents/MacOS/ei-rebuild`. Windows, Linux and macOS use the same
CMakeLists.txt and `src/main.cpp`; CMake selects the platform and graphics backends. Shaders are compiled once by
sokol-shdc for Direct3D, OpenGL and Metal. There is no separate native build or startup path.

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

- All game and library translation units compile as native ARM64 C++20. Compiler and OS selection stays in
  `src/platform` (the `__APPLE__` lines in platform_sdl.cpp, floating_point.cpp's ARM no-op). CMakeLists.txt turns off
  multiply/add contraction (`-ffp-contract=off`).
- The POSIX backend (`src/platform/posix/platform_posix.cpp`) implements files, mappings, INI reads/writes, sockets,
  process locking and user-data paths. File lookup normalizes backslashes and folds case a component at a time, so
  `res\database.res` opens `Res/database.res` on a case-sensitive volume. Exact spelling wins; ambiguous folded
  matches sort consistently.
- The renderer is main's, whole: shared shaders (docs/notes/renderer.md, "Shader authoring") and every transfer drawn
  with sokol. `src/platform/metal/graphics_metal.mm` implements `platform/graphics.h` on Metal and holds sokol's
  implementation:
  - The drawable: an SDL Metal view's `CAMetalLayer` (BGRA8, `framebufferOnly` off for reads), `drawableSize` the
    window's pixels, `displaySyncEnabled` the present mode's vsync. sokol presents it at the end of its pass;
    `EndDrawable` only lets it go.
  - `UpdateBuffer` (the GpuPool's ranges): sokol's `write_unsealed` buffer is one `MTLBuffer`, not rotated, so a
    CPU write would race the frames in flight. The bytes go into a compute pass of sokol's (`sg_mtl_compute_command_encoder`),
    4 KB a `setBytes` with a word-copy kernel compiled at first use, ordered with sokol's draws in its command buffer.
  - `Readback`: Metal runs a queue's command buffers in the order they were enqueued, and sokol enqueues the frame's
    at its first pass, so a read first commits sokol's work (`sg_commit` after an empty compute pass, which gives
    the commit a command buffer: one without moves sokol's uniform buffers on without waiting for the GPU), then
    blits into a shared buffer of its own (`Staging`, pitch `sg_query_row_pitch`) and waits.
  - No GPU timestamps (`GpuTimer::Begin` refuses), no lost device (`Lost` is empty), `MaxSamples` from
    `supportsTextureSampleCount`.
- Retina windows request high pixel density, and the game sees pixels only: platform_sdl.cpp makes the window
  VideoResolution / the display mode's pixel density in points (1280x800 points for 2560x1600 on a 2x display), so its
  drawable holds the configured resolution, and scales mouse positions, motion, ScreenToClient / ClientToScreen and
  the cursor position by the window's pixel density. Nothing is written back to the settings. Native 2x is
  VideoResolution 2560x1600. SDL's cursor uses point dimensions and a matching hotspot, with the original pixels
  attached as a high-density image. The renderer uses main's SDL cursor; no cursor thread or Metal cursor draw remains.
- Apple's libc++ has no float `std::from_chars`: settings_document.cpp's `ParseFloat` reads with `strtof` (the C
  locale) where the library does not define `__cpp_lib_to_chars`, refusing what from_chars refuses.
- `port_platform` (tests/port/port_check.cpp) covers path folding, file/mapping behavior, INI updates,
  single-instance exclusion, UDP, math edge cases, caller capture and the settings checks
  (tests/vectors/replay_settings.cpp). `port_render` (tests/port/render_check.cpp) runs the interface canvases' glyph
  cases (tests/render/glyph_cases.cpp, the ones tools/ui_check runs on Direct3D 11) on Metal.

## Remaining verification and features

The scenario harness remains Windows-only; no claim of simulation equivalence across compilers follows from
these native checks. Fullscreen, resizing across displays, Intel Macs, multiplayer, long play sessions and movie
playback still need coverage. GPU timing queries are not implemented on Metal.

Open: a read-back mid-frame commits sokol's frame early, after which sokol appends to its stream buffers from the start
of their other slot; GeometryStream (render_buffers.cpp) keeps the offsets of the renderer's frame, so a stream bound
again after such a read in the same frame would read the wrong slot. No case is known; watch for it.
