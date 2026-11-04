# Platform layer

The game's code reaches the operating system through one header, `src/platform/platform.h` (namespace `platform`, one
section per area), that carries no Windows SDK type (UTF-8 `std::string` / `const char*`, fixed-width integers, our own
enums and structs, a native window as an opaque `void*`), and one implementation file per backend. SDL3
(`src/platform/sdl/`) has the window, its events, the cursor, the mouse, the clipboard, the message box, the clocks and
the threads' priority in the builds without the harness; the harness's builds have their own backend for those areas on
SDL ("SDL3 and the harness" below). The folder calls are std::filesystem's (`src/platform/files.cpp`, every system).
Windows (`src/platform/win32/`) has the rest; it is the only place of the game's code that includes `<windows.h>`. On
Linux and macOS POSIX (`src/platform/posix/`) has it (linux.md, macos.md). The renderer (`src/render`) reaches its
device and the window's swap chain through SDL's GPU API ("Graphics" below). Game code does not change between the
systems.

## Files

| File | What |
|---|---|
| `src/platform/platform.h` | the layer: every area below, in that order |
| `src/platform/sdl/platform_sdl.cpp` | SDL3's areas (builds without the harness): the clocks, the message box, the threads, the window and its events, the cursor, the mouse, the clipboard |
| `src/platform/sdl/sdl_threads.cpp`, `sdl_keys.cpp` | the threads' priority, and the keys' codes, of every build on SDL (the harness's too) |
| `src/platform/sdl/sdl_input.cpp` | SDL's keyboard and mouse events as the platform's (`sdl_input::Translator`): the game's backend and the harness's recording of real play (harness.md, "Recorded play") take the same translation |
| `tests/verify/platform/` | the harness's platform backend: harness_platform.cpp (the window, events, clocks, cursor, mouse and clipboard on SDL, in place of platform_sdl.cpp) and game_answers.cpp (the calls a harness run answers itself, in place of game_calls.cpp and system_path.cpp) |
| `src/platform/files.cpp` | the folder calls (create, remove, rename, copy, exists, free space) on std::filesystem, every system, each path through `paths.h` |
| `src/platform/paths.h`, `system_path.cpp` | the file calls' path seam: `SystemPath(path, use)` (system_path.cpp: the path itself; the harness's builds: its overlay, below) and each backend's `NativePath` |
| `src/platform/win32/platform_win32.cpp` | the Windows remainder, in every build: UTF-8 to the wide calls (`text::Wide` / `Narrow`), paths, the machine, the file objects, whole files and folder searches, the other game window (the one-instance check), the keyboard's names and layouts, sockets |
| `src/platform/win32/game_calls.cpp`, `src/platform/posix/game_calls.cpp` | the calls a harness run answers itself (below, "The harness's answers"), each system's; the harness's builds leave them out and take tests/verify/platform's |
| `src/platform/win32/platform_win32.h`, `src/platform/posix/platform_posix.h` | each backend's own declarations for its files: Windows' `text::Wide` / `Narrow` and `win32::BringForward`; POSIX's one-instance lock (`posix::OtherInstanceRunning`, game_calls.cpp's BringOtherInstanceForward) |
| `src/platform/posix/platform_posix.cpp` | the remainder on Linux and macOS: paths, files, mappings and folder searches with Windows' case rules, the one-instance lock, key names, sockets |

- CMakeLists.txt's ei_platform builds src/platform/sdl/*.cpp, files.cpp, system_path.cpp and src/platform/win32/*.cpp
  or (Linux, macOS) src/platform/posix/*.cpp (with EI_HARNESS but platform_sdl.cpp, game_calls.cpp and system_path.cpp).
  The sound and the movies are not the platform's:
  src/audio and src/video, one implementation each (sound.md). platform.h carries no backend's type. Still Windows'
  values, on purpose: the virtual-key codes and the set-1 scan codes (the bindings file and the interface keep them;
  SDL's backend maps its keys to them).
- Inside `namespace platform` an unqualified call of a Windows function finds the platform function of that name first
  (`Sleep`, `ScreenToClient`, `ClientToScreen`, `SetCursor`, `ActivateKeyboardLayout`): the backend calls
  the system as `::X`. When the files were merged, EndProcess's `Sleep(INFINITE)` (system.cpp, which did not see the
  clock header) would have become `platform::Sleep`, the harness's clock; it is `::Sleep`.

## The rule and its check

- Game code (`src/game`, `src/lib`, `src/standalone`) includes `platform/platform.h` and calls `platform::X`. A new
  call of the system goes into an area of platform.h and its section of the backend, not into the game file.
- The win32 implementation calls the system directly, the wide (UTF-16) functions where text crosses (paths, key
  names, the volume label; `text::Wide` / `Narrow`), except the calls a harness run answers itself (game_calls.cpp,
  "The harness's answers" below), which the goldens depend on. Every file and folder call takes its path through
  `SystemPath` ("Files and the harness's overlay" below).
- A call the harness never answered stays out of its answers: code from another module that read the real clock
  (net_second_master.cpp's `GetTickCount`) must not start reading the harness's.
- `tools/check_platform.py` (a build check) fails on a file under `src/` outside `src/platform` that includes
  `<windows.h>` or another system header (`<dinput.h>`, `<wingdi.h>`, `<mmsystem.h>`, `<winsock2.h>`, `<process.h>`,
  `<d3d12.h>`, ...: the script lists them) or a header of the Windows backend (`platform/win32/`; comments left out).
  It sees a file's own includes only.
- Game, renderer and platform layer share one `Rect`, `Point` and `Size` (lib/rect.h: global structs with operators and
  value methods; the float pair is `Vector2`, vecmath.h) and `std::uint32_t` / `std::int32_t` for DWORD, COLORREF,
  UINT, LONG.
- A native window crosses the layer as `void*` (`platform::NativeWindow`); game code holds no `HWND`. app.h's
  `g_mainWindow` and texture.h's device window are `platform::NativeWindow`; the renderer's window code casts it.

## Areas

platform.h's sections (the sockets in `platform::net`):

| Area | What | Windows calls behind it |
|---|---|---|
| Clocks | `Milliseconds()` (timeGetTime), `TickCount()` (GetTickCount), `Sleep`, `SecondsSince1970()` (a save's date: lib/calendar.h turns it into the local date and text) | SDL (`std::chrono::system_clock` for the date); harness builds: the harness's clocks and fixed date (harness_clock.h) |
| The message box | `ShowMessageBox(owner, text, caption, buttons, icon)` | SDL; harness builds: the harness's answer |
| Paths | `ProgramPath()` (the system's separators), `CurrentFolder()`, `SetCurrentFolder`, `UserDataFolder()`, `ArgumentsAfterSeparator` | GetModuleFileNameW, GetCurrentDirectoryW, SetCurrentDirectoryW, GetEnvironmentVariableW |
| The machine | `FontSmoothing()`; `PrimaryDesktopSize`, `EnvironmentVariable`, `EndProcess` | SystemParametersInfoW (game_calls.cpp: the harness fixes smoothing off); the last three direct |
| Files and folders | `CreateFolder`, `RemoveFolder`, `RemoveFile`, `RenameFile`, `CopyFileOver`, `PathExists`, `IsFile`, `FreeDiskSpace` (files.cpp, std::filesystem); `FolderEntries` (a folder search's `FolderEntry`s: name, path, size, folder); `ReadWholeFile` / `WriteWholeFile`; `File` (`FileMode` Read / Create / WriteExisting; also text lines: `WriteLine`), `MappedFile` (read only), `FileError` | every path through `SystemPath` (the harness's overlay, below); CreateFileW, FindFirstFileW, CreateFileMappingW direct; the file objects' writes through game_calls.cpp's `win32::WriteFile` (the harness's failed-write watch) |
| The window and its events | `StartWindowSystem(name)`, the game window (`CreateGameWindow`, `ShowGameWindow`), `Event`s (keys with modifiers, UTF-8 text, a mouse button with its click count, the wheel in notches, the focus, the close request), the event queue (`EventWaits`, `NextEvent`, `TakeEvent`, `PostQuit`), screen / client points, `CoverMonitor`, `SetClientSize`, `DisplaySizes` (the display's modes' sizes for the resolution row) | SDL; harness builds: the harness's backend on SDL (harness_platform.cpp) |
| The other game | `BringOtherInstanceForward(name, title)`: another copy runs, its window brought forward | Windows in every build: FindWindowW and the foreground calls (game_calls.cpp: the harness finds none); POSIX a lock file |
| The cursor | the game's shapes (`MakeCursor`, `FreeCursor`, `SetCursor`), the hold (`HoldCursor`), its confinement to the window (`ConfineCursor`), its position in game points (`MapCursorPoints` for the scaled frame) | SDL; harness builds: the same SDL cursors with their pixels kept for the checks, the harness's own mouse |
| The keyboard | `KeyName(scan)`, `ActivateKeyboardLayout(language, fallback)`, `IsLetter(character)` (the message box's edit field) | Windows in every build (SDL has no key names in the layout's language, no layout choice): GetKeyNameTextW, the keyboard layout calls, IsCharAlphaW |
| The clipboard | `CopyText`, `ClipboardText` (Rebirth's console) | SDL; harness builds: direct |
| Threads | `MakeThreadTimeCritical()` (the sound's stream thread); the threads are `std::thread`, their waits `std::condition_variable` or a join, locks `std::mutex` / `std::recursive_mutex` | SDL in every build (sdl_threads.cpp) |
| Key codes | the game's key codes (`kKeyEscape`, `kKeyReturn`, ...: the virtual-key values the bindings file keeps) | none |
| The mouse | `TakeMouseMotion()`: the relative motion since the last call (the buttons and the wheel are events) | SDL's motion events; harness builds: the harness's own motion (harness_platform.cpp) |
| Sockets | IPv4 UDP / TCP sockets (`Socket`, pointer-sized), `Address` (sockaddr_in's 16 bytes), `Select` on `SocketGroup`s, host name and look-ups, `LastError()` as our `Error`; Winsock starts with the first socket call (1.1, never cleaned up). Byte order and `ParseIpv4` (inet_addr) are lib/ipv4.h's | Winsock (ws2_32); closing, binding, connecting, sending, receiving and select's call through game_calls.cpp (the harness keeps them on 127.0.0.1) |

The sockets mirror BSD's one to one, each failing as Winsock's does, because the game relies on the failures:
`Select` leaves only the ready sockets in its sets, so MasterLinkTick's failure check after a select that found the
socket not ready asks with an empty set, which always fails (a refused master connect never closes the link there);
NetAddress::Format looked the address's name up with the whole 16-byte address as the bytes, so it never found one
and always gave the dotted number (now `NetAddress::ToString`);
an `Address` whose family is not 2 fails in the system. The second master (net_second_master.cpp, EI_Plugin's) goes
through the same platform calls (the harness's answers touch only its binds and datagrams); its look-up thread, wait and times are standard C++
(`std::thread`, a future, `steady_clock`): the real clock, never the harness's.

The two clocks stay two: the original reads `timeGetTime` and `GetTickCount` in different places, and the harness
drives each separately, so a call keeps the clock it had. QueryPerformanceCounter is not used for them: it would
bypass the harness's clocks.

## Files and the harness's overlay

The Files and folders section is the game's file objects (the old lib/file_io.h): `File` (a binary file opened in one
of three `FileMode`s: Read, Create (truncating), WriteExisting; a text file's lines written with `WriteLine`, "\r\n"
ends), `ReadWholeFile` (the text readers cut it with lib's `str::Lines`, text mode's line ends; false for a missing or
unreadable file, the system's error code on request), `WriteWholeFile` (a temporary file renamed over the target),
`MappedFile` (a whole file mapped read only: the archives and the registry files) and `FolderEntries` (a folder
search: the entries in the system's order, "." and ".." included, each with its path in the searched folder). A
failed read, write or seek throws `FileError` (the code and the path in what()); `ReadAt` and `MappedFile::Open`
return false instead. The folder calls (files.cpp) are std::filesystem's on every system, their paths made from the
UTF-8 text (MSVC's `std::filesystem::path` takes a narrow string in the ANSI code page, so `NativePath` builds it from
the wide text). `RemoveFile` refuses a folder and `RemoveFolder` a file, as DeleteFileW and RemoveDirectoryW did
(ui_saveload's delete loop passes "." and ".."); `RenameFile` refuses an existing target (MoveFileW).

Every open shares the file for reading and writing (FILE_SHARE_READ | FILE_SHARE_WRITE, never FILE_SHARE_DELETE, so
removing an open file still fails; ReadWholeFile's open shares deleting too, the file is closed before it returns)
with FILE_ATTRIBUTE_NORMAL; MFC's per-call share bits, the archives' FILE_FLAG_NO_BUFFERING / FILE_FLAG_RANDOM_ACCESS
hints and the sector-size test that chose between them are gone. Most of the game's opens were exclusive. An open
that the old sharing refused would now succeed; none happens: PL11b's temporary check logged, over a main and a
Rebirth suite, only read opens of a file already open for reading (the .res archives and maps/zq.mq), which were
shared before too, and no sharing violation (PL11s removed it).

**One path seam** (src/platform/paths.h, PL15): every file and folder call of every backend takes its path through
`platform::SystemPath(path, use)` and then the backend's `NativePath`. `PathUse` says what the call does with it:
`Read` (opens, maps, searches, exists, free space), `Write` (creates, truncates, removes, a rename's target) or
`Update` (a change of a file that is there: WriteExisting, a rename's source). The game's builds link
system_path.cpp's (the path itself); the harness's builds link harness.cpp's, which maps the path into the run's
overlay and answers nothing else:

- `Read`: ReadPath (the run's copy of a save or install file when it has one, else the file itself); config\movie.ini
  maps to "" (missing: no startup movies).
- `Write` / `Update`: WritePath (the overlay's copy; `Update` copies the install's file there first, so a rename moves
  the copy, never the install's file); the save trace sees the write.

The overlay used to answer eight calls itself (CreateFolder, RemoveFile, RenameFile, CopyFileOver, RemoveFolder,
FileExists, the open and the folder search); now it answers one question, where a path is. The seam is plain C++
(std::string_view in, std::string out), so a harness on Linux (H47) maps paths with the same function. Rebirth's
look-ups (IsFile, FolderEntries) and the settings file (ReadWholeFile / WriteWholeFile next to the exe or in the
user's data folder) pass the seam too, though addon.dll's and the settings' calls never reached the harness's answers
before: their paths are outside the saves and the install's overlaid files, so ReadPath and WritePath leave them as
they are. Nothing of the harness sits below the seam: a write still under an install after the mapping is refused
there (standalone-verification.md, "No run writes into an install").

No file outside src/platform calls Windows.

### The harness's answers

game.exe's import slots (a table of ours, generated from its import directory until PL14: gameapi.h, import_table.h,
image_imports.cpp, tools/gen_iat.py) existed only so that the harness could patch them. Now the harness answers the
game's calls in its own builds, as harness_platform.cpp does for SDL's areas: CMakeLists.txt leaves
src/platform/<system>/game_calls.cpp and src/platform/system_path.cpp out of them (EI_GAME_CALLS), and
tests/verify/platform/game_answers.cpp defines the same functions, one file for both systems:

- `SystemPath` (above: harness.cpp's MapPath), `FontSmoothing` (off), `BringOtherInstanceForward` (none runs).
- `File::Write` (the file objects' writes, with the file's path: a failed write fails the run; "diskfull" fails a
  file's writes as on a full disk; harness_crash.h).
- `net::Bind`, `SendTo`, `ReceiveFrom`, `Close`, `Connect`, `Send`, `Receive` and `Select` (binds and datagrams on
  127.0.0.1, a pair's network; harness_net.h, harness_pair.h).
- The clocks, sleeps and date are harness_clock.h's (harness_platform.cpp asks them); the message box is the
  harness's answer (harness_platform.cpp's ShowMessageBox).

Each answer is the system's call while the harness is not running (a verify build started without EI_HARNESS), through
tests/verify/system/harness_system.h: the harness's own system calls (threads, clocks, files, faults, stacks, a pair's
shared memory, the window's concealment), system_win32.cpp and system_posix.cpp. The harness hooks nothing of the
system: no import slot, hot patch or module patch is left. The redirects the game no
longer reached went with the slots: the window's creation and showing (SDL's window), the cursor's position (the
harness's backend), WaitForSingleObject and GetMessageA (the threads and events are SDL's), GetSystemTime.

## Window and events

### SDL3 and the harness

A framework that owns the loop and the swap chain does not fit (the game has nested loops, our renderer its own swap
chain, the harness its own input). SDL3 does: it pumps from the game's own loops (`SDL_PollEvent` inside `EventWaits` /
`NextEvent` / `TakeEvent`, `SDL_WaitEvent` while NextEvent waits), and the renderer keeps its device and swap chain on the
window (gpu.h's `ClaimWindow`; the NativeWindow the game holds is `SDL_PROP_WINDOW_WIN32_HWND_POINTER`).

- **Linkage: static.** third_party/SDL (3.4.18, zlib licence) is built by CMakeLists.txt as SDL3-static with only
  video, audio and the GPU API on Direct3D 12 and Vulkan (no 2D renderer, camera, joystick, haptic, HID, sensor, power,
  dialog, OpenGL), before the
  game's compile options, into ei-rebuild.exe: the exe stays one file dropped into a game folder (S23), for x86 and
  x64 alike. Its sources compile without a warning under its own flags.
- **The harness has its own platform backend on SDL** (tests/verify/harness_platform.cpp), built in its builds in
  place of the game's platform_sdl.cpp (the threads, sdl_threads.cpp, and the key tables are shared): an SDL window
  shown without activation, every system event SDL takes dropped (the real keyboard and mouse never reach a harness
  game; counted for the report), the harness's events queued for the game's loops, its steps run before EventWaits /
  TakeEvent look (where its runs took posted input; false holds the events for the close-all pass), a key's character
  queued by NextEvent as the harness's TranslateMessage posted it, the focus changes only the harness's focus steps
  make (queued ahead of the waiting input, as Windows told the game at once), its message box answers, and the harness's clocks, date and mouse position (harness_clock.h's virtual
  and held clocks and fixed date, harness.cpp's own mouse). The backend is SDL and
  the harness's own system layer (tests/verify/system), the same on both systems. Its steps queue the
  platform's events (harness_input.h: a key with its scan code, Alt's own key holding Alt, a character as UTF-8 text
  with its key's scan code, a double click's second press with two clicks, the wheel's turn), as the games saw them
  on the window-procedure path (gone with DirectInput: PL12). SDL's translation of real
  input is checked by tools/input_check against Windows' layouts. A live recording (`recordinput live`, harness.md
  "Recorded play") takes the real keyboard and mouse through that translation (sdl_input.h) instead of dropping them,
  in a window shown and activated, its cursor held as the game's backend holds it.
- What stays Windows in every build: the other game window (FindWindowW on the class name, which SDL_RegisterApp
  gives SDL's window class: "EIGAME"), the key names and keyboard layouts, the paths (SDL's pref path is the roaming
  folder and creates it, its base path is the folder, not the file: the settings file would move), the
  files and sockets. Their POSIX versions (src/platform/posix/platform_posix.cpp,
  PL9b): the program's path from `/proc/self/exe`, the user data folder from SDL's pref path, the one-instance check
  as an advisory `flock` on a lock file named after the name and title (another process's window is not raised), the
  layout's key codes, Ctrl characters and key names from SDL's keymap (`SDL_GetKeyFromScancode`: what the key types,
  read as Windows' layouts give theirs; linux.md, "What differs from Windows", LX4), `ActivateKeyboardLayout` a no-op
  (text follows the system's layout), `IsLetter` by `iswalpha_l`.

What SDL's backend does (platform_sdl.cpp):

- **The window**: hidden until ShowGameWindow, its client area the game's size. Full screen a borderless SDL window at
  the screen's top left, which `CoverMonitor` moves and sizes over its display (no display mode change, no SDL full
  screen). Windowed a decorated, resizable window centred in the primary display's usable bounds, its client area made
  smaller when it and the borders would not fit there (the harness's backend places its window the same way). `SetClientSize` (a resolution picked in the options, windowed) sizes
  and places it the same way in the usable bounds of the display it is on (restored first when maximised, in points on
  macOS) and waits for the system (`SDL_SyncWindow`). `DisplaySizes`: `SDL_GetFullscreenDisplayModes` of the window's display, each
  mode's size in pixels (width times `pixel_density`), `platform::ChooseSizes` (display_sizes.h: deduped, narrowest
  first, none under 800x600 or above `SDL_GetDesktopDisplayMode`'s size); the harness's backend lists 19 fixed sizes. Hints:
  Alt+F4 is the game's (`SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4` 0), closing the window is a request
  (`SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE` 0), mouse events only over the window (`SDL_HINT_MOUSE_AUTO_CAPTURE` 0, as Windows
  gave them). SDL makes the process per-monitor DPI aware: its coordinates are pixels.
- **Every event waits in the queue** for the game's loops: focus gained / lost, minimised, restored become `Focus`
  (only on a change of active / minimised), the close request (and SDL's quit) `CloseRequested`. SDL keeps the cursor
  set over its window and the screen saver off itself; exposures are not passed on (the renderer presents on its own).
- **Keys** (sdl_keys.cpp): the virtual-key code and the set-1 scan code (0x100 extended) from the key's place (a table of
  US positions); the keys a layout moves (the digit row, the letters, the main block's punctuation) take the active
  layout's code (`platform::LayoutKey`: Windows' MapVirtualKeyExW, so AZERTY's M key is VK_M; on Linux and macOS
  `sdl_keys::KeyOfTyped` of what the key types in SDL's keymap), else the US place's; tools/input_check checks the codes
  and the control characters against every layout the machine has (a build check), and on Linux
  (input_check_linux.cpp, ctest's `port_input`) codes, Ctrl characters and names on X11's us, ru, fr and de layouts. A key event carries SDL's repeat and Shift, Ctrl and Alt. Text: SDL's typed
  text, UTF-8, with the scan code of the last key pressed (the interface drops the console key's character by it), and
  the control characters a key types, which SDL's text leaves out (Backspace, Tab, Enter, Escape; with Ctrl the active
  layout's, `platform::LayoutControlCharacter`: Ctrl+[ 0x1B, Ctrl+] 0x1D, Ctrl+\\ 0x1C on a US layout, none
  for those keys on a Russian one; otherwise Ctrl+letter 1..26, Ctrl+Escape 0x1B, Ctrl+Backspace 0x7F, Ctrl+Enter 0x0A;
  none with Alt).
- **The mouse**: positions in the window's pixels, a button (left, right, middle, the side buttons) with SDL's click
  count, the wheel in notches (`SDL_MOUSEWHEEL_FLIPPED` turned back). `TakeMouseMotion` gives the motion events' sum
  since the last call.
- **The cursor**: the game's pictures as SDL colour cursors (`MakeCursor`: `SDL_CreateColorCursor` on an RGBA
  surface; the renderer makes them, docs/notes/renderer.md section 9, R40), shown with `SDL_SetCursor` +
  `SDL_ShowCursor`, hidden with `SDL_HideCursor` (null shape). The position is `SDL_GetGlobalMouseState`, placed with
  `SDL_WarpMouseGlobal`. `HoldCursor`: the window in relative mode with a one-pixel mouse rectangle at the point (the
  hints keep the cursor visible there, not centred, the motion scaled as the pointer's; the motion's fractions are
  carried to the next event); `ConfineCursor`: `SDL_SetWindowMouseGrab` (the harness's backend only logs the request; the game
  asks every main loop pass and at the focus's changes, when the answer changed, app_confine.cpp: there is no option,
  the cursor is confined while the window is active and the camera's edge pan is available); `SetCursorPosition` lets it go before it warps, since SDL puts the pointer back where its
  motion went when relative mode ends.
- **The clocks**: both are `SDL_GetTicks` plus a day (`kClockStart`), so no read is near 0 (the game keeps 0 as
  "never" in times it compares with them, and the original ran on machines that had been up for a while).
  `Sleep(0)` yields (`std::this_thread::yield`); SDL_Delay(0) would return at once.
- **Threads**: `std::thread`; the sound's stream thread makes itself time-critical as it starts
  (`SDL_SetCurrentThreadPriority`, SetThreadPriority's 15). Its only wait was the close's, now a join.
- **The message box**: `SDL_ShowMessageBox`, owned by the game window when the caller names it; closed without a
  button, an OK / Cancel box answers Cancel.

### How the events flow

- `platform::StartWindowSystem("EIGAME")` (app_main.cpp, before the one-instance check). The game's loop (app_main.cpp
  RunMainLoop) asks `EventWaits`, takes the next event with `NextEvent` and passes it to `app::DispatchInput` (the
  interface; the focus and the close request go on to `app::HandleWindowEvent` without the interface's close-all) or
  `app::HandleWindowEvent`. `PumpMessages` takes events with `TakeEvent` and gives them to HandleWindowEvent (input
  dropped); the movie loop takes Escape and Space and gives the rest to HandleWindowEvent.
- app.cpp's dispatcher hands the interface an event's own data: a key's code, scan code and repeat, the system keys
  (Alt without Ctrl, or F10: Alt+F4 quits, a release is printed to the console with the original's message data,
  Alt+Tab deactivates), each character of a Text event with its scan code, a press with an even click count as a
  double click, the mouse's position, the wheel's turn signed at 120 a notch.
- **Mouse positions in game points.** The game takes its mouse positions as screen coordinates of a window at 0, 0 the
  size of its resolution: the cursor position (the cursor, the global map) is read and placed without converting, mouse
  events come in the window's pixels. The window is anywhere (windowed it is
  centred and movable) and the frame may be scaled into it (render::FrameInWindow), so these are always mapped to frame
  pixels: present_window.cpp maps mouse events (`MouseToGame`) and gives the cursor calls its mapping with
  `platform::MapCursorPoints` at each present; every game call of the cursor's position goes through platform.h's cursor
  calls.
- Key codes stay the virtual-key values, as `platform::kKey...` constants (the bindings file and the options keep the
  values); a port gives its key events the same values.

## Graphics

The renderer has no graphics layer of the platform's: SDL's GPU API is one (Direct3D 12 on Windows, Vulkan elsewhere),
and `src/render/gpu.h` is the renderer's own thin layer over it (renderer.md section 15c): the device, the window's
swap chain and present modes, textures, buffers, passes, downloads and the frame's GPU time. No render file includes a
system graphics header. The window's size in pixels is platform.h's `WindowPixelSize` (SDL:
`SDL_GetWindowSizeInPixels`, false while minimised). MXCSR's control bits are checked once a frame and set back when a
driver changed them (render.cpp's `CheckFloatControl`; float-maths.md, "The control word").

## Left for the next areas

- The renderer's own timings are `std::chrono::steady_clock` (render_internal.h's `Ticks` / `kMsPerTick`), not the
  game's clocks; the log (src/platform/log.cpp) is standard C++ too (`std::mutex`, `std::ofstream`, the time of day
  through lib's `FormatLocalTime`).
- The renderer's own threads (render.cpp) stay its own.

## Plain C++ in place of the system

What needed no system at all is standard C++, each checked equal to the call it replaced:

- **Asset text** (asset_text.cpp): the code page's upper half as a table (1250, 1251 and 1252, taken from
  MultiByteToWideChar: every byte one character, the undefined ones the C1 control of their number) and a strict UTF-8
  decoder (MB_ERR_INVALID_CHARS's rules: shortest form, no surrogates, nothing past U+10FFFF, nothing cut short); a
  character the code page lacks becomes '?', one past U+FFFF "??" (WideCharToMultiByte's two units). Checked against
  MultiByteToWideChar / WideCharToMultiByte on 10.2 million cases. `text::CodePoints` (the interface's drawing and
  measuring) decodes UTF-8 the same way; text that is not UTF-8 is taken in the asset code page.
- **Formatting**: the script errors and DXException's texts are `std::vsnprintf` into a 1 KB buffer (the formats use
  only %s and %d).
- **The titles' twips** (ui_scrolling_surface.cpp): MulDiv's rounding (half away from zero, -1 for a zero divisor or an
  overflow) on 64 bits; equal to USER32's on 20 million cases but a divisor of INT_MIN (the divisor is a positive DPI).
- **The settings** (settings.h): a plain ini of our own read and written through typed accessors (GetInt / GetBool /
  GetFloat / GetString and their setters). Nothing reads or writes the registry.
- **Bitmaps** (dib.h): `DibFileHeader`, `DibHeader`, `DibColour` with the .bmp layout (14, 40 and 4 bytes).
- **Text format bits**: render/text_format.h (DrawText's DT_* values) for the interface's DrawTextIn.
- **Return addresses** (the harness's observers in random.cpp, crt.cpp, core_streams.cpp): lib/caller.h's
  `EI_RETURN_ADDRESS()` / `EI_CALLER_FRAME()`, MSVC's intrinsics (declared without <intrin.h>) or clang's builtins.
  Macros, so that they read the frame of the function that uses them: an inline function is not inlined at /Ob0 (the
  debug build) and would name itself as the caller.
- **Dates** (lib/calendar.h, PL15): `LocalDateTime` / `FormatLocalTime` of `platform::SecondsSince1970()` by the C
  library's `localtime_s` (`localtime_r`) and `strftime`: a save's date, the log's time of day.
- **IPv4 text and byte order** (lib/ipv4.h, PL15): `ParseIpv4` is inet_addr's parser (one to four parts, decimal, octal
  or hexadecimal, the last filling the rest, whitespace ending the text, inet_addr's overflow rules), equal to Winsock
  on 6 million strings (a scratch sweep; tests/unit/ipv4.cpp keeps the edge cases); `ToNetwork` / `FromNetwork` are
  htons / ntohs.
- **Rebirth's flags** (lib/rebirth.cpp): plain data and specs; EI_REBIRTH with `getenv_s`. The
  settings file's [patches] is read by the start-up through settings.h (C66a), no reader of its own.
- The world's wincurs\Df0000.cur cursor (LoadCursorFromFileA) was loaded and never used: gone.

## Rectangles

`lib/rect.h` (`Rect::IsEmpty`, `Contains`, `Offset`, `Intersection`, `Union`, `Width`, `Height`) is plain integer
code with USER32's rules, so the UI and figure files no longer call SetRect, OffsetRect, IntersectRect ... (not an area
of the layer: no system call is left). An empty rectangle has right <= left or bottom <= top; `Intersection` and
`Union` return the all-zero rectangle when nothing is left (`Union` ignores an empty operand); `Contains` excludes the
right and bottom edges; `Offset` and the aggregate initialisation do not normalise. Layout code builds rectangles from
others with `Inset` (all edges, dx / dy, or each edge), the `LeftStrip` / `RightStrip` / `TopStrip` / `BottomStrip`
bands inside an edge and `Rect::Sized(topLeft, size)`; none of them normalises either. The edge rules were compared with
USER32 on 56 million edge and random rectangle pairs (H33h's scratch test); `std::min` / `std::max` are written
`(std::min)` in the header because windows.h defines the macros. A mouse event's position is a `Point` (the
`LeftDown(int buttons, Point point)` handlers).

## What the game depends on (2026-10-09 estimate, kept for scale)

| Area | Original dependency | Note |
|---|---|---|
| Window, messages, cursor, input | USER32 (151 imports), DINPUT (1) | SDL3 since PL9 (the harness's builds: the window procedure) |
| 2D text and surfaces | GDI32 (83) | the native interface (P track) replaces GDI |
| Rendering | DDRAW, D3D7 through COM | isolated: SDL_GPU behind the DDraw7 / D3D7 boundary |
| Sound / video | Miles mss32 (47), DSOUND (1); Bink (12) | none since PL6: SDL3's audio device (miniaudio until PL9), our Bink 1 decoder |
| Network | WSOCK32 (22, by ordinal) | BSD sockets map almost 1:1 (PL5) |
| Files, threads, time | KERNEL32 (159), WINMM (5), ADVAPI32 (11) | PL3, PL4; time done here |
