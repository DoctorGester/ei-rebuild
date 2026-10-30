# Platform layer

The game's code reaches the operating system through `src/platform/`: small headers by area that carry no Windows SDK
type (UTF-8 `std::string` / `const char*`, fixed-width integers, our own enums and structs, a native window as an
opaque `void*`), and one implementation per system. Today that is `src/platform/win32/`, the only place of the game's
code that includes `<windows.h>` or calls game.exe's imports (`gameapi::X`). The renderer's Direct3D backend
(`src/render`) keeps its own Windows calls. The x64 build (A3a) and the ports after it replace or add an
implementation folder; game code does not change.

## The rule and its check

- Game code (`src/game`, `src/lib`, `src/standalone`) includes `platform/<area>.h` and calls `platform::X`. A new call
  of the system goes into an area header and its win32 file, not into the game file.
- The win32 implementation calls game.exe's imports as `gameapi::X`, as the game code did, never as `::X`, unless the
  call never went through an import slot. Many slots hold something else than the export: the harness's routes (the
  virtual and held clocks on `timeGetTime`, `GetTickCount`, `Sleep`, `WaitForSingleObject`; the fixed date on
  `GetLocalTime` / `GetSystemTime`; the message hooks) and the image's UTF-8 routes (`src/game/system_text.cpp`'s
  `TEXT_ROUTE` list: paths, files, `MessageBoxA`, window text). tools/gen_iat.py's `routed()` lists them; the goldens
  depend on them. Keeping every `gameapi::X` call (moved, not removed) also keeps gameapi.h, which is in the harness's
  reference key, unchanged.
- A call that never used a slot stays direct: code from another module that read the real clock
  (net_second_master.cpp's `GetTickCount`) must not start reading the harness's.
- `tools/check_platform.py` (build.cmd, through tools/build_checks.py) lists the files under `src/` outside
  `src/platform`, `src/render` and `src/hook` (the harness's hook framework) that include `<windows.h>` or call
  `gameapi::` (comments left out). A file not in `config/platform_files.txt` fails the build; after moving a file's
  calls run `python tools/check_platform.py --update` (the list only shrinks). The dashboard shows the count
  (`platform_files`, "Files calling Windows"): 97 before the layer, 78 after the first areas.
- The window, its event loop and input will most likely move onto sokol_app (the renderer already uses sokol_gfx):
  the layer has no window or input abstraction of its own, and a native window crosses it as `void*`
  (`sapp_win32_get_hwnd`'s type).

## Areas

| Header | What | Windows calls behind it |
|---|---|---|
| `clock.h` | `Milliseconds()` (timeGetTime), `TickCount()` (GetTickCount), `Sleep`, `LocalTime()` | the slots of timeGetTime, GetTickCount, Sleep, GetLocalTime |
| `debug.h` | `DebugOutput` | OutputDebugStringA |
| `message_box.h` | `ShowMessageBox(owner, text, caption, buttons, icon)` | the MessageBoxA slot (UTF-8 route) |
| `paths.h` | `ExePath()`, `CurrentFolder()` | the GetModuleFileNameA / GetCurrentDirectoryA slots (UTF-8 routes) |
| `system.h` | `PageSize()` | GetSystemInfo |

The two clocks stay two: the original reads `timeGetTime` and `GetTickCount` in different places, and the harness
drives each separately, so a call keeps the clock it had. sokol_time (QueryPerformanceCounter) is not used for them: it
would bypass the harness's clocks and change the replays.

## Left for the next areas

- Calls left in files other tickets were changing when the layer started: app_main.cpp (its main-loop
  `timeGetTime`, the `MessageBoxA` disk and disc warnings, `GetModuleHandleA` / `GetProcAddress`,
  `GetVolumeInformationA`, `CreateDirectoryA`, the window calls), src/standalone/startup.cpp and standalone_main.cpp
  (`GetModuleFileNameA`, `SetCurrentDirectoryA`, `Sleep(INFINITE)`, `GetModuleHandleA`; these run before or beside
  the import table and call the system directly), lib/file_io.cpp (`GetCurrentDirectoryA` / `GetFullPathNameA`,
  PL3), resource.cpp's `GetSystemTime` (the mapped file's write time, PL3), app_helpers.cpp's disk space lookup (PL3).
- The renderer's QueryPerformanceCounter / Frequency (render.cpp, render_perf.cpp, render_textures.cpp) stay its own.
- PL2, window and input (likely sokol_app): app.cpp, app_main.cpp, present_window.cpp, ui_console_window.cpp,
  ui_edit_field.cpp, ui_cursor_setup.cpp, ui_font.cpp (`SystemParametersInfoA`), key_bindings.cpp
  (`GetKeyNameTextA`), ui_interface_create.cpp (keyboard layouts), the cursor calls (`GetCursorPos` / `SetCursorPos`
  in ui_camera_input, ui_global_map, ui_global_map_input), bink_video.cpp's message pump.
- PL3, files: lib/file_io.cpp, resource.cpp, resource_stream.cpp (file mapping, `mmio*`), random.cpp's seed file
  reads, world_game.cpp, party_file.cpp, camera_object.cpp, ui_saveload.cpp, ui_hero_screen_click.cpp, ini_file.cpp
  (`GetPrivateProfile*`), and the direct (`::X`) callers: mod_folders.cpp, console_addon.cpp,
  script_addon_commands.cpp (file attributes, find), settings_file.cpp (its own file, environment and monitor
  calls). asset_text.cpp's code page conversions go with the text rules (UTF-8, C34). config/platform_files.txt is the
  full list.
- PL4, threads: lib/thread.cpp, the critical sections and events of sound_manager.cpp, ui_cursor_frame.cpp,
  ui_cursor_setup.cpp.
- PL5, sockets: net_socket_driver.cpp, net_link.cpp, net_second_master.cpp, static_init.cpp's `WSAStartup`,
  world_game.cpp's `gethostname`.
- PL6, audio and video: sound_manager.cpp (Miles, `DirectSoundEnumerateA`), bink_video.cpp (Bink).
- Rectangles (`SetRect`, `OffsetRect`, `IntersectRect`, ... in about 30 ui and figure files) go to our own functions
  (H33h), not to the layer; `wvsprintfA` (app.cpp, dx_errors.cpp, script_parse.cpp) to the CRT's formatting.
- system_text.cpp, the UTF-8 routes themselves, is Windows code that belongs under src/platform/win32 once the
  file and window areas no longer call through the A slots.
- The import layer's own files (lib/import_table.h, import_prototypes.h, gameapi.h / .cpp, win_api.h) stay in
  src/lib while the harness's reference key holds them.

## What the game depends on (2026-10-09 estimate, kept for scale)

| Area | Original dependency | Note |
|---|---|---|
| Window, messages, cursor, input | USER32 (151 imports), DINPUT (1) | sokol_app planned (PL2) |
| 2D text and surfaces | GDI32 (83) | the native interface (P track) replaces GDI |
| Rendering | DDRAW, D3D7 through COM | isolated: sokol_gfx behind the DDraw7 / D3D7 boundary |
| Sound / video | Miles mss32 (47), DSOUND (1); Bink (12) | seams with null backends (PL6), then A1 / A2 |
| Network | WSOCK32 (22, by ordinal) | BSD sockets map almost 1:1 (PL5) |
| Files, threads, time | KERNEL32 (159), WINMM (5), ADVAPI32 (11) | PL3, PL4; time done here |
