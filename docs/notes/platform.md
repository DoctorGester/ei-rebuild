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
  (`platform_files`, "Files calling Windows"): 97 before the layer, 78 after the first areas, 61 after the window
  and input (PL2), 58 after the sockets (PL5), 48 after the files and the start-up (PL3, PL1b).
- A native window crosses the layer as `void*` (`platform::NativeWindow`); game code keeps an `HWND` only where a
  shared header holds one (app.h's `g_mainWindow`, texture.h's device window; `HWND` comes from lib/win_types.h, not
  `<windows.h>`).

## Areas

| Header | What | Windows calls behind it |
|---|---|---|
| `clock.h` | `Milliseconds()` (timeGetTime), `TickCount()` (GetTickCount), `Sleep`, `LocalTime()` | the slots of timeGetTime, GetTickCount, Sleep, GetLocalTime |
| `debug.h` | `DebugOutput` | OutputDebugStringA |
| `message_box.h` | `ShowMessageBox(owner, text, caption, buttons, icon)` | the MessageBoxA slot (UTF-8 route) |
| `paths.h` | `ExePath()`, `CurrentFolder()`; `ProgramPath()`, `UserDataFolder()`, `SetCurrentFolder` | the GetModuleFileNameA / GetCurrentDirectoryA slots (UTF-8 routes); the last three direct (wide calls) |
| `system.h` | `PageSize()`, `FontSmoothing()`; `PrimaryDesktopSize`, `EnvironmentVariable`, `EndProcess` | GetSystemInfo; the SystemParametersInfoA slot (the harness fixes smoothing off); the last three direct |
| `files.h` | `CreateFolder`, `RemoveFolder`, `RemoveFile`, `RenameFile`, `CopyFileOver`, `FreeDiskSpace`, `VolumeLabel`; `ReadWholeFile` / `WriteWholeFile`, `PathExists`, `IsFile`, `FolderEntries` | the CreateDirectoryA, RemoveDirectoryA, DeleteFileA, MoveFileA, CopyFileA (harness redirects, UTF-8 routes) and GetVolumeInformationA slots; GetDiskFreeSpaceExW direct; the last five direct (below) |
| `file.h` | `File`, `TextFile`, `FileMapping`, `FileFind` (attributes as `IsDirectory` / `IsHidden` / `IsSystem`), `OpenFile`, `FileExists`, `FileError`, `LastError()` | the CreateFileA, ReadFile, WriteFile, SetFilePointer, CreateFileMappingA, MapViewOfFile, FindFirstFileA, GetFileAttributesA ... slots (UTF-8 routes), so the harness's save / install redirection and a mod's hooks apply |
| `ini.h` | `IniSectionEntries`, `IniString`; `IniInt` | the GetPrivateProfileSectionA / StringA slots (not routed: the config\*.ini names are ASCII); `IniInt` direct |
| `window.h` | the window class, `ProgramInstance()`, the game window, `Event`s, the event queue (`EventWaits`, `NextEvent`, `TakeEvent`, `DefaultHandling`, `DispatchEvent`, `PostQuit`), screen / client points, `CoverMonitor` | the window and message slots (the harness hooks CreateWindowExA, ShowWindow, PeekMessageA, FindWindowA); the screen mapping, monitor calls and `ProgramInstance` direct |
| `cursor.h` | the cursor's shape, its position in game points (`MapCursorPoints` for the scaled frame) | the GetCursor / SetCursor / GetCursorPos / SetCursorPos slots (the harness's own mouse) |
| `keyboard.h` | `KeyName(scan)`, `ActivateKeyboardLayout(language, fallback)` | the GetKeyNameTextA (UTF-8 route) and keyboard layout slots |
| `clipboard.h` | `CopyText`, `ClipboardText` (Rebirth's console) | direct |
| `net.h` | IPv4 UDP / TCP sockets (`Socket`, pointer-sized), `Address` (sockaddr_in's 16 bytes), `Select` on `SocketGroup`s, host name and look-ups, `Start` / `Stop`, `LastError()` as our `Error` | the WSOCK32 slots (19 of its 22 imports; `WSAGetLastError` direct, game.exe does not import it) |

The sockets mirror BSD's one to one, each failing as Winsock's does, because the game relies on the failures:
`Select` leaves only the ready sockets in its sets, so MasterLinkTick's failure check after a select that found the
socket not ready asks with an empty set, which always fails (a refused master connect never closes the link there);
`HostNameAt` takes the bytes it is given (NetAddress::Format passes the whole 16-byte address, so it finds no name);
an `Address` whose family is not 2 fails in the system. The second master (net_second_master.cpp, EI_Plugin's) goes
through the same slots, as the plugin's calls did except gethostbyname and select (no harness route touches those
in its runs); its look-up thread, wait and times are standard C++ (`std::thread`, a future, `steady_clock`): the
real clock, never the harness's.

The two clocks stay two: the original reads `timeGetTime` and `GetTickCount` in different places, and the harness
drives each separately, so a call keeps the clock it had. sokol_time (QueryPerformanceCounter) is not used for them: it
would bypass the harness's clocks and change the replays.

## Files and the direct calls

`file.h` is the game's file objects (the old lib/file_io.h): `File` (a binary file, MFC's open flags), `TextFile`,
`FileMapping`, `FileFind` (a folder search with CFileFind's read-ahead), `OpenFile` (fopen) and `FileExists`. A failed
read, write or seek throws `FileError`; seek and error behaviour are as before the move. No SDK type is in the header
(the search state is a private `Search` struct of the win32 file, a handle never leaves `File`).

A few callers never went through the game's slots and must not start to (the harness's routes would change what they
see in verify runs). Each has its own platform function that calls the system directly:

- Rebirth's file look-ups (mod_folders, console_addon, script_addon_commands: addon.dll called its own imports):
  `platform::IsFile`, `platform::FolderEntries` (the UTF-8 versions in game/system_text.cpp, called by name).
- The settings file (settings_file.cpp: next to the exe or in the user's data folder, whatever a harness run
  redirects): `ReadWholeFile` / `WriteWholeFile` (wide calls), `ProgramPath`, `UserDataFolder`, `PrimaryDesktopSize`;
  its lock is a `std::mutex`.
- The standalone's start-up (startup.cpp, standalone_main.cpp's `EnterGameFolder` and exit paths): `ProgramPath`,
  `EnvironmentVariable`, `SetCurrentFolder`, `PathExists`, `IniInt`, `ProgramInstance` and `EndProcess`
  (TerminateProcess, then `Sleep(INFINITE)` as the old code, so the call never returns). `app::GameMain` takes
  `platform::NativeInstance`.
- standalone_main.cpp, host.cpp and image_imports.cpp stay on the list as the win32 entry: they are the loader (the
  vectored and unhandled exception handlers, `VirtualProtect` on another module's import table, `GetModuleHandleA` /
  `GetProcAddress` of the DLLs the slots resolve to, the DPI call, the process id for the log). A port has its own
  entry; nothing of the game's code is in them.

## Window and events

### sokol_app: not now (PL2, 2026-10-30)

The window, its events, the cursor and the keyboard are our own platform headers with a win32 implementation, not
sokol_app. What sokol_app would need and the game cannot give it yet:

- **The loop.** `sapp_run` owns the message loop and calls a frame callback; with `SOKOL_NO_ENTRY` we keep `WinMain`
  but still hand the loop over (sokol_app has no "pump events" call, by design). The game's loop is not one frame per
  call: start-up plays movies and loading screens before the loop (bink_video.cpp's own message loop, PlayMovies,
  ShowLoadingMovie), map loads present progress frames from inside a pass, and interface switches pump messages from
  deep inside game code (app::PumpMessages from ui_interface_frame, ui_interface_input, ui_create_screen, ...). Each of
  those would have to return to sokol_app's loop between frames: the game turned into a state machine, or run on a
  fiber that yields at every present and pump.
- **The present.** sokol_app makes the D3D11 device and swap chain and presents after each frame callback. Our
  renderer makes its own device (render.cpp) and swap chain on the game window (render_window.cpp:
  CreateSwapChainForHwnd, its own vsync and tearing flags, the frame scaled into the window) and presents where the
  game draws, also from the nested loops above; the cursor overlay presents on its own (render_cursor.cpp).
- **The harness.** The goldens depend on how input reaches the game. The harness posts window messages to the game
  window (keys with their scan codes, WM_CHAR, mouse, double clicks), runs its steps and counts frames inside the
  game's PeekMessageA (the import slot, at ei::mainloop's PeekBeforeFrame / PeekAfterFrame sites), holds messages
  while a close-all is pending, subclasses the window procedure (SetWindowLongPtrA: the focus steps call it with
  WM_ACTIVATE), answers GetCursorPos / SetCursorPos and FindWindowA through their slots, and checks the window's class
  name ("EIGAME", the windowtitle step). sokol_app peeks with PeekMessageW directly (no slot), registers its own class
  ("SOKOLAPP", no CS_DBLCLKS: no double clicks) and turns messages into its own events between frames, a frame later
  than the game takes them now.
- **Keys and the cursor.** sokol_app's key codes are positions (scan codes mapped to a US layout); the game binds
  virtual-key codes, names keys with GetKeyNameText in the active layout and picks the keyboard layout itself
  (ActivateKeyboardLayout). sokol_app cannot place the cursor (SetCursorPos: the camera and global map drags) or read
  it outside events, and gives no character repeat count or key flags (the interface reads the scan code from them).
- What would fit: its full screen is a borderless window over the monitor (no display mode change), as ours is.

So the layer's own `platform::Event` carries what the game reads; the win32 implementation turns the window messages
into it and keeps every call on its slot, and the goldens do not move. A later implementation (sokol_app once the loop
runs one frame per call, or a library with a pump call such as SDL3, which keeps our loop and our swap chain) replaces
src/platform/win32's window, cursor and keyboard files; the game code does not change.

### How the events flow

- `platform::RegisterWindowClass(instance, "EIGAME", app::HandleWindowEvent)`: the class's window procedure turns each
  message into an Event and returns the handler's answer. The game's loop (app_main.cpp RunMainLoop) asks
  `EventWaits` (PeekMessageA, no removal, inside the harness's site), takes the next with `NextEvent` (GetMessageA +
  TranslateMessage) and passes it to `app::DispatchInput` (the interface) or `app::HandleWindowEvent`, never through
  DispatchMessage: as the original, posted input does not pass the window procedure (the harness's subclass sees only
  sent messages). `PumpMessages` takes events with `TakeEvent` (no translation) and gives them to HandleWindowEvent;
  the movie loop dispatches only Paint events (`DispatchEvent`).
- Events keep the message's values the game read: the virtual-key code, the repeat count and the key flags (the high
  half of the key data, sign-extended: scan code, `kKeyAltDown`, ...), the UTF-16 unit of a typed character (app.cpp
  joins surrogate pairs), the mouse buttons (MK_ bits), positions as signed 16-bit values; the dispatcher hands the
  interface positions and the wheel's turn as unsigned 16-bit values, as it read them from the message before.
  `Event::native` is the message itself, for `DefaultHandling` (DefWindowProcA) and `DispatchEvent` only. The
  default handling goes to the message's own window: the original's loop passed every message it took to the game
  window's procedure as the game window's, which differs only for a message posted to another window of the thread
  or to none (none the game handles).
- Unknown messages are `Other`; the QueryCancelAutoPlay message (registered the first time a message is not one of
  the others) is `AutoPlay`. The game's own wheel message 0xCC54 (a fixed number in the registered messages' range,
  which nothing sends) is gone.
- The frame scaled into the window: present_window.cpp maps mouse events (`MouseToGame`) and gives the cursor calls
  its mapping with `platform::MapCursorPoints` at each present (it replaced the GetCursorPos / SetCursorPos slots
  before); every game call of the cursor's position goes through platform/cursor.h.
- Key codes stay the virtual-key values (lib/win_user.h's VK_ constants, which the bindings file and the options keep);
  a port gives its key events the same values.

## Left for the next areas

- standalone_main.cpp, host.cpp and image_imports.cpp: the loader (see "Files and the direct calls"); they stay.
- The renderer's QueryPerformanceCounter / Frequency (render.cpp, render_perf.cpp, render_textures.cpp) stay its own.
- The key codes as our own constants (a platform/keys.h in place of lib/win_user.h's VK_ values in about 20 ui
  files), and `g_mainWindow` / the device's window as `platform::NativeWindow` (app.h, texture.h, the sound and
  renderer start-up): PL2a.
- resource_stream.cpp's `mmio*` (sound files) and its FindFirstFileA for the file size (PL6 moves them with the
  audio).
- PL4, threads: lib/thread.cpp, the critical sections and events of sound_manager.cpp, ui_cursor_frame.cpp,
  ui_cursor_setup.cpp.
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
