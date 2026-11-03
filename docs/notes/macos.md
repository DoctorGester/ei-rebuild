# macOS

The game builds natively on Apple Silicon with Apple Clang from the same CMakeLists.txt and `src/main.cpp` as on
Windows and Linux: SDL3 for the window, input, sound and threads, POSIX for the rest of the platform
(src/platform/posix, as on Linux), and the renderer on SDL_GPU's Metal driver (src/render/gpu.cpp; renderer.md
section 15c), its programs' MSL from the HLSL (renderer.md, "Shader authoring"). The user's macos-port branch ran it
against the Russian game data on sokol's Metal backend (the animated menu, sound, the new-game dialog and the first
campaign map); since MAC4 / R43h the Mac draws through SDL_GPU like the other systems, and nothing of the branch's
Metal backend (graphics_metal.mm) is left. macOS cannot be built on the Windows machine: CMake's Apple parts are kept
right by reading, and the Mac's own checks are below ("Checks on the Mac"). The scenario harness does not build on
macOS.

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

## Run

For normal play, from the repository:

```sh
python3 tools/run_macos.py --play
```

It runs until you quit and keeps `run/macos/` for saves, settings, a copied `Config` and the logs. The data defaults to
`~/workspace/ei`; `--data` chooses it on the first run. `run/macos/ei_settings.ini` keeps the resolution and the other
settings across launches. The start movies are left out unless `--intro` is given. `[video] FullScreen = false` is a
centred window with the macOS title bar and controls; `true` a borderless window over the monitor.

Bounded runs (at most 60 s; exit 124 when the time ran out):

```sh
python3 tools/run_macos.py --data ~/workspace/ei --seconds 60
```

Each makes a new folder under `build/macos/runs/`, links the game data's read-only folders (Res, Maps, Movies, Stream,
Camera) into it, copies `Config` and the settings, and points every writable path there (`EI_GAME_ROOT`, `EI_OUT_DIR`,
`EI_SETTINGS_PATH`); it keeps `stdout.txt`, `ei-rebuild.log` and the settings. Do not recursive-delete such a folder
while it holds the links. The app started directly without those variables expects the data beside the executable.

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
  depth is D32F) and no Mailbox present mode (gpu.h's fallbacks take Vsync for it).

## Checks on the Mac

`ctest --test-dir build/macos --output-on-failure`: `port_platform` (path folding, the Bink container read through
platform::File, files and mappings, the .ini reader, the one-instance lock, UDP, maths, the settings checks),
`port_render` (the glyph cases on Metal, the window's pixel size, the client / screen round trip, the cursor's hold;
the X11 present read-back is Linux's only), `port_shaders` (every program's MSL and its pipelines on Metal) and
`port_gpu` (gpu.cpp on Metal).

What has not been run on a Mac since MAC4 / R43h (the first Mac build must check it):
- the configure and build: the shader toolchain against the Vulkan SDK's DXC, the MSL (spirv-cross), the bundle;
- ctest's four checks, `port_shaders` and `port_gpu` first (Metal's compile of every MSL, the formats, downloads, the
  swap chain's present modes);
- a play run (`tools/run_macos.py --play`): the menu, a zone, the camera's right-button turn with the cursor visible
  at the press point, the cursor's shapes at 2x, full screen and windowed, a resize, a movie and the loading screen;
- the shadows (the receivers' `shadow_range` is the texture shadercross's MSL had put at slot 0) and the smooth option
  (multisampling with the depth copy).

Not covered yet: Intel Macs, displays of other densities, multiplayer, long sessions; the overlay's GPU time comes
from the fences as on the other drivers.
