# Linux

The game builds on Linux (x86-64, gcc 13 or clang 18) from the same CMakeLists.txt as on Windows: the game and
library code unchanged, SDL3 for the window, input, sound and threads, POSIX for the rest of the platform
(src/platform/posix/platform_posix.cpp), and the renderer on sokol_gfx's OpenGL 4.5 backend
(src/platform/graphics_gl.cpp; docs/notes/platform.md, "Graphics"). The harness, the function vectors' replay and the
offline checks against Windows' drawing and keys stay Windows' only.

## Build

Packages (Ubuntu 24.04; SDL3 finds the rest itself, Wayland's too when its development packages are there):

```sh
sudo apt-get install cmake ninja-build libx11-dev libxext-dev libxcursor-dev libxi-dev libxrandr-dev libxss-dev \
    libxfixes-dev libxtst-dev libxkbcommon-dev libgl-dev libegl-dev libasound2-dev libpulse-dev
```

sokol-shdc, the shader compiler (cmake/shaders.cmake looks in build/tools, then PATH, or takes EI_SHDC):

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
  one-instance lock, UDP, the settings document's checks (tests/vectors/replay_settings.cpp).
- `port_render` (tests/port/render_check.cpp): the interface canvases' glyph cases on OpenGL against their CPU
  reference (tests/render/glyph_cases.cpp, which tools/ui_check runs on Direct3D 11).

The Linux workflow (.github/workflows/linux.yml) runs both on GitHub's Ubuntu runner for every push.

## Run

`build/linux/ei-rebuild` plays the game folder named by `EI_GAME_ROOT`, else the one it sits in; `EI_SETTINGS_PATH`
pins the settings file. The folder's `Work path` and `CD-ROM Path` are read as the game folder itself. Game data
names are matched as Windows matches them: `res\database.res` opens `Res/Database.RES` (one folder at a time; the
exact spelling first). Arguments for the game follow `--`.

## What differs from Windows

- The cursor is not drawn again between frames during a long load: the GL context is the main thread's
  (`KeepPresent` refuses).
- No lost context is looked for (it needs a robust context).
- The keys: SDL's scancodes through sdl_keys.cpp's US rules; no layout's own key codes or control characters
  (`LayoutKey` / `LayoutControlCharacter`), and the key names are SDL's English ones.
- Not played with real data yet: menus, maps, movies, sound, saves and multiplayer are unchecked on Linux.
