# Platform layer

The game's code reaches the operating system through one header, `src/platform/platform.h` (namespace `platform`,
one section per area), that carries no Windows SDK type (UTF-8 `std::string` / `const char*`, fixed-width integers,
our own enums and structs, a native window as an opaque `void*`), and one implementation file per backend. SDL3
(`src/platform/sdl/`, PL9) has the window, its events, the cursor, the mouse, the clipboard, the message box, the clocks
and the threads' priority in the builds without the harness; the harness's builds have their own backend for those
areas on SDL ("SDL3 and the harness" below). The folder calls are std::filesystem's (`src/platform/files.cpp`, every
system). Windows (`src/platform/win32/`) has the rest; it is the only place of the game's code that includes
`<windows.h>`. On Linux POSIX (`src/platform/posix/`) has it (docs/notes/linux.md). The renderer (`src/render`) reaches its device and the window's drawable through
`src/platform/graphics.h` ("Graphics" below). Game code does not change between the systems.

## Files

| File | What |
|---|---|
| `src/platform/platform.h` | the layer: every area below, in that order |
| `src/platform/sdl/platform_sdl.cpp` | SDL3's areas (builds without the harness: release, debug, play, x64): the clocks, the message box, the threads, the window and its events, the cursor, the mouse, the clipboard |
| `src/platform/sdl/sdl_threads.cpp`, `sdl_keys.cpp` | the threads' priority, and the keys' codes, of every build on SDL (the harness's too) |
| `tests/verify/harness_platform.cpp` | the harness's builds' window, events, clocks, cursor, mouse and clipboard on SDL, in place of platform_sdl.cpp |
| `src/platform/files.cpp` | the folder calls (create, remove, rename, copy, exists, free space) on std::filesystem, every system, each path through `paths.h` |
| `src/platform/paths.h`, `system_path.cpp` | the file calls' path seam: `SystemPath(path, use)` (system_path.cpp: the path itself; the harness's builds: its overlay, below) and each backend's `NativePath` |
| `src/platform/win32/platform_win32.cpp` | the Windows remainder, in every build: UTF-8 to the wide calls (`text::Wide` / `Narrow`), paths, the machine, the file objects, whole files and folder searches, the other game window (the one-instance check), the keyboard's names and layouts, sockets |
| `src/platform/win32/game_calls.cpp` | the calls a harness run answers itself (below, "The harness's answers"); the harness's builds leave it out and take tests/verify's definitions |
| `src/platform/win32/platform_win32.h` | the Windows backend's own declarations: `text::Wide` / `Narrow` and game_calls.cpp's system calls (`platform::win32::WriteFile`, `Select`), for the backend's files and the harness |
| `src/platform/posix/platform_posix.cpp` | the remainder on Linux: paths, files, mappings and folder searches with Windows' case rules, the one-instance lock, key names, sockets |
| `src/platform/graphics.h` | the renderer's device, the window's drawable, read-backs and GPU timestamps ("Graphics" below) |
| `src/platform/gl/graphics_gl.cpp` | its OpenGL 4.5 backend on Linux (built into ei_render with sokol_gfx's implementation) |
| `src/platform/d3d11/graphics_d3d11.h`, `.cpp` | its Direct3D 11 backend (built into ei_render with sokol_gfx's implementation), and what only Windows has: the vertical blank switch's rules, the Direct3D objects the remaining Direct3D render files use |
| `src/platform/floating_point.cpp` | `FloatingPointGuard` (platform.h; built into ei_render with the graphics backend: the offline checks link the renderer without the game) |

- CMakeLists.txt's ei_platform builds src/platform/sdl/*.cpp, files.cpp, system_path.cpp and src/platform/win32/*.cpp
  or (Linux) src/platform/posix/*.cpp (with EI_HARNESS but platform_sdl.cpp, game_calls.cpp and system_path.cpp);
  ei_render builds the graphics backend
  (src/platform/d3d11 or src/platform/gl) and floating_point.cpp. The sound and the movies are not the platform's:
  src/audio and src/video, one implementation each (docs/notes/audio.md). platform.h carries no backend's type:
  `MouseDevice` holds a backend's private `Device` and `Event::window` is the event's window. Still Windows' values, on
  purpose: the key codes and key flags (the bindings file and the interface keep them; SDL's backend maps its keys to
  them).
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
- `tools/check_platform.py` (build.cmd, through tools/build_checks.py) fails on a file under `src/` outside
  `src/platform` that includes `<windows.h>` or another system header (`<dinput.h>`, `<wingdi.h>`, `<mmsystem.h>`,
  `<winsock2.h>`, `<process.h>`, `<d3d11.h>`, ...: the script lists them), a header of a Windows backend
  (`platform/win32/`, `platform/d3d11/`) or calls sokol's Direct3D functions (`sg_d3d11_*`) (comments left out; until
  PL14 also game.exe's import slots, `gameapi::`). Until 2026-10-31 it was a ratchet on a list of files (config/platform_files.txt), which reached zero
  when the import table moved into src/platform/win32. The dashboard shows the count
  (`platform_files`, "Files calling Windows"): 97 before the layer, 78 after the first areas, 61 after the window
  and input (PL2), 58 after the sockets (PL5), 48 after the files and the start-up (PL3, PL1b). That check looked at `<windows.h>` and
  `gameapi::` only; widened to the other system headers (PL2a) it counted 12 more files (win_types.h, win_user.h,
  dib.h, ...), and the threads, the key codes and the mouse (PL4, PL2a) took 3 off: 57; our own rectangle functions
  (below) took 31 off: 26. The last game-side users (PL7) took 17 off: 9; the sound and movie files (PL6b), the sound
  files' reads (PL7b) and lib/win_types.h (PL7a) left it: 5, the win32 entry (below) and the import layer's headers.
  The rule took in src/render with graphics.h (MAC3f): 9; the transfers drawn with sokol (MAC3g) took
  render_buffers, render_compose and render_ui off, and the cursor's present behind graphics.h (MAC3h; gone
  with R40) render_cursor: 5, none of them in src/render.
- The check sees a file's own includes only. lib/win_user.h (winuser.h for the DT_* constants) let ui_edit_field.cpp
  call `IsCharAlphaW` unnoticed (now `platform::IsLetter`); win_user.h is gone. lib/win_types.h (windef.h's types)
  was the last such header: 113 game files used its RECT, POINT, SIZE, DWORD, COLORREF, LONG and UINT. They use our
  own `Rect`, `Point`, `Size` (lib/rect.h: global structs of Windows' RECT, POINT and SIZE layout; the win32 side
  static_asserts the sizes where it hands one to the system) and `std::uint32_t` / `std::int32_t` (DWORD, COLORREF,
  UINT, LONG).
- A native window crosses the layer as `void*` (`platform::NativeWindow`); game code holds no `HWND`. app.h's
  `g_mainWindow` and texture.h's device window are `platform::NativeWindow`; the renderer's window code casts it.

## Areas

platform.h's sections (the sockets in `platform::net`, the sound in `platform::audio`, the movies in
`platform::video`):

| Area | What | Windows calls behind it |
|---|---|---|
| Clocks | `Milliseconds()` (timeGetTime), `TickCount()` (GetTickCount), `Sleep`, `SecondsSince1970()` (a save's date: lib/calendar.h turns it into the local date and text) | SDL (`std::chrono::system_clock` for the date); harness builds: the harness's clocks and fixed date (harness_clock.h) |
| The message box | `ShowMessageBox(owner, text, caption, buttons, icon)` | SDL; harness builds: the harness's answer |
| Paths | `ProgramPath()` (the system's separators), `CurrentFolder()`, `SetCurrentFolder`, `UserDataFolder()`, `ArgumentsAfterSeparator` | GetModuleFileNameW, GetCurrentDirectoryW, SetCurrentDirectoryW, GetEnvironmentVariableW |
| The machine | `FontSmoothing()`; `PrimaryDesktopSize`, `EnvironmentVariable`, `EndProcess` | SystemParametersInfoW (game_calls.cpp: the harness fixes smoothing off); the last three direct |
| Files and folders | `CreateFolder`, `RemoveFolder`, `RemoveFile`, `RenameFile`, `CopyFileOver`, `PathExists`, `IsFile`, `FreeDiskSpace` (files.cpp, std::filesystem); `VolumeLabel`; `FolderEntries` (a folder search's `FolderEntry`s: name, path, size, folder); `ReadWholeFile` / `WriteWholeFile`; `File` (`FileMode` Read / Create / WriteExisting; also text lines: `WriteLine`), `MappedFile` (read only), `FileError` | every path through `SystemPath` (the harness's overlay, below); GetVolumeInformationW (game_calls.cpp: the disc label); CreateFileW, FindFirstFileW, CreateFileMappingW direct; the file objects' writes through game_calls.cpp's `win32::WriteFile` (the harness's failed-write watch) |
| The window and its events | the window class, the game window, `Event`s, the event queue (`EventWaits`, `NextEvent`, `TakeEvent`, `DefaultHandling`, `DispatchEvent`, `PostQuit`), screen / client points, `CoverMonitor` | SDL; harness builds: the harness's backend on SDL (harness_platform.cpp) |
| The other game window | `FindClassWindow(className, title)`, `WindowProcess`, `BringToFront`, `ProgramInstance()` | Windows in every build: FindWindowW (game_calls.cpp: the harness finds none), GetWindowThreadProcessId, the foreground calls, `ProgramInstance` |
| The cursor | the game's shapes (`MakeCursor`, `FreeCursor`, `SetCursor`), the hold (`HoldCursor`), its position in game points (`MapCursorPoints` for the scaled frame) | SDL; harness builds: the same SDL cursors with their pixels kept for the checks, the harness's own mouse |
| The keyboard | `KeyName(scan)`, `ActivateKeyboardLayout(language, fallback)`, `IsLetter(character)` (the message box's edit field) | Windows in every build (SDL has no key names in the layout's language, no layout choice): GetKeyNameTextW, the keyboard layout calls, IsCharAlphaW |
| The clipboard | `CopyText`, `ClipboardText` (Rebirth's console) | SDL; harness builds: direct |
| Threads | `MakeThreadTimeCritical()` (the sound's stream thread); the threads are `std::thread`, their waits `std::condition_variable` or a join, locks `std::mutex` / `std::recursive_mutex` | SDL in every build (sdl_threads.cpp) |
| Key codes | the game's key codes (`kKeyEscape`, `kKeyReturn`, ...: the virtual-key values the bindings file keeps) | none |
| The mouse | `MouseDevice`: the mouse's motion (`Open`, `SetActive`, `Read`, `Close`), failures reported to the caller's `MouseReport` | SDL's motion events; harness builds: the harness's own motion (harness_platform.cpp) |
| Sockets | IPv4 UDP / TCP sockets (`Socket`, pointer-sized), `Address` (sockaddr_in's 16 bytes), `Select` on `SocketGroup`s, host name and look-ups, `LastError()` as our `Error`; Winsock starts with the first socket call (1.1, never cleaned up). Byte order and `ParseIpv4` (inet_addr) are lib/ipv4.h's | Winsock (ws2_32); closing, binding, connecting, sending, receiving and select's call through game_calls.cpp (the harness keeps them on 127.0.0.1) |

The sockets mirror BSD's one to one, each failing as Winsock's does, because the game relies on the failures:
`Select` leaves only the ready sockets in its sets, so MasterLinkTick's failure check after a select that found the
socket not ready asks with an empty set, which always fails (a refused master connect never closes the link there);
NetAddress::Format looked the address's name up with the whole 16-byte address as the bytes, so it never found one
and always gave the dotted number (now `NetAddress::ToString`);
an `Address` whose family is not 2 fails in the system. The second master (net_second_master.cpp, EI_Plugin's) goes
through the same platform calls (the harness's answers touch only its binds and datagrams); its look-up thread, wait and times are standard C++ (`std::thread`, a future, `steady_clock`): the
real clock, never the harness's.

The two clocks stay two: the original reads `timeGetTime` and `GetTickCount` in different places, and the harness
drives each separately, so a call keeps the clock it had. sokol_time (QueryPerformanceCounter) is not used for them: it
would bypass the harness's clocks and change the replays.

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
they are. The harness's kernel32 / ntdll install guard (harness_install.h) stays below the seam: it catches what the
overlay misses.

No file outside src/platform calls Windows.

### The harness's answers (PL14)

game.exe's import slots (a table of ours, generated from its import directory until PL14: gameapi.h, import_table.h,
image_imports.cpp, tools/gen_iat.py) existed only so that the harness could patch them. Now the harness answers the
game's calls in its own builds, as harness_platform.cpp does for SDL's areas: CMakeLists.txt leaves
src/platform/win32/game_calls.cpp and src/platform/system_path.cpp out of them, and tests/verify defines the same
functions:

- harness.cpp: `SystemPath` (above), `FontSmoothing` (off), `FindClassWindow` (none; the disc check's answers start
  there).
- harness_crash.cpp: `win32::WriteFile` (the file objects' writes, with the file's path: a failed write fails the
  run; a path holding "diskfull" fails as a full disk).
- harness_startup.cpp: `VolumeLabel` (the "discprompt" labels).
- harness_net.cpp: `net::Bind`, `SendTo`, `ReceiveFrom`, `Close`, `Connect`, `Send`, `Receive` and `win32::Select`
  (binds and datagrams on 127.0.0.1, a pair's network).
- The clocks, sleeps and date are harness_clock.h's (harness_platform.cpp asks them).

A game function whose body is one system call is the seam itself; where platform_win32.cpp's function does more (the
file objects' writes, the socket groups) the seam is the call (platform_win32.h). Each answer is the
system's call while the harness is not running (a verify build started without EI_HARNESS). The redirects the game no
longer reached went with the slots: the window's creation and showing (SDL's window), the cursor's position (the
harness's backend), WaitForSingleObject and GetMessageA (the threads and events are SDL's), GetSystemTime.

## Window and events

### SDL3 and the harness (PL9)

sokol_app was not taken (it owns the loop and the swap chain; the reasons were the game's nested loops, our renderer's
own D3D11 swap chain, the harness's input). SDL3 fits: it pumps from the game's own loops (`SDL_PollEvent` inside
`EventWaits` / `NextEvent` / `TakeEvent`, `SDL_WaitEvent` while NextEvent waits), and the renderer keeps its device and
swap chain on the window's HWND (`SDL_PROP_WINDOW_WIN32_HWND_POINTER`: the NativeWindow the game holds).

- **Linkage: static.** third_party/SDL (3.4.18, zlib licence) is built by CMakeLists.txt as SDL3-static with only
  video and audio (no GPU, renderer, camera, joystick, haptic, HID, sensor, power, dialog, OpenGL, Vulkan), before the
  game's compile options, into ei-rebuild.exe: the exe stays one file dropped into a game folder (S23), for x86 and
  x64 alike. Its sources compile without a warning under its own flags.
- **The harness has its own platform backend on SDL** (tests/verify/harness_platform.cpp), built in its builds in
  place of the game's platform_sdl.cpp (the threads, sdl_threads.cpp, and the key tables are shared): an SDL window
  shown without activation, every system event SDL takes dropped (the real keyboard and mouse never reach a harness
  game; counted for the report), the harness's events queued for the game's loops, its steps run before EventWaits /
  TakeEvent look (where its runs took posted input; false holds the events for the close-all pass), a key's character
  queued by NextEvent as the harness's TranslateMessage posted it, the activation changes only the harness's focus
  steps make, its message box answers, and the harness's clocks, date and mouse position (harness_clock.h's virtual
  and held clocks and fixed date, harness.cpp's own mouse). The backend file
  itself is SDL only; the harness's answers are its Windows part. Its steps still speak
  window messages; harness_input.cpp makes each into the event the window procedure's translation made, so the games
  see what they saw on the window-procedure path (platform_win32_window.cpp, gone with DirectInput, `Event::native`
  and `DefaultHandling`: PL12). SDL's translation of real input is checked by tools/input_check against Windows'
  layouts.
- What stays Windows in every build: the other game window (FindWindowW on the class name, which SDL_RegisterApp
  gives SDL's window class: "EIGAME"), the key names and keyboard layouts, the paths (SDL's pref path is the roaming
  folder and creates it, its base path is the folder, not the file: the settings file would move), the
  files and sockets. Their POSIX versions (src/platform/posix/platform_posix.cpp,
  PL9b): the program's path from `/proc/self/exe`, the user data folder from SDL's pref path, the one-instance check
  as an advisory `flock` on a lock file named after the class and title (the other process's id in it), key names
  for the set-1 scan codes through SDL's, `ActivateKeyboardLayout` a no-op (text follows the system's layout), no
  layout keys or control characters of the system's (sdl_keys.cpp's rules), `IsLetter` by `iswalpha_l`.

What SDL's backend does (platform_sdl.cpp):

- **The window**: hidden until ShowGameWindow, its client area the game's size. Full screen a borderless SDL window
  at the screen's top left, which `CoverMonitor` moves and sizes over its display (no display mode change, no SDL full
  screen). Windowed a decorated, resizable window centred in the primary display's usable bounds, its client area made
  smaller when it and the borders would not fit there (the harness's backend places its window the same way). Hints:
  Alt+F4 is the game's (`SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4` 0), closing the window is a request
  (`SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE` 0), mouse events only over the window (`SDL_HINT_MOUSE_AUTO_CAPTURE` 0, as
  Windows gave them). SDL makes the process per-monitor DPI aware (its default, as the loader does): its coordinates
  are pixels.
- **Window events go to the handler at once**, as the window procedure's sent messages did: focus gained / lost,
  minimised, restored become `Activate` (only on a change of active / minimised), exposed becomes `Paint`, the close
  request (and SDL's quit) `Destroy`. No `SystemCommand` or `AutoPlay` events: SDL keeps the cursor set over its
  window and keeps the screen saver off itself. Input waits in a queue for the game's loops.
- **Keys** (sdl_keys.cpp): the virtual-key code and the set-1 scan code (0x100 extended) from the key's place (a
  table of US positions); the keys a layout moves (the digit row, the letters, the main block's punctuation) take the
  active layout's code (`platform::LayoutKey`: Windows' MapVirtualKeyExW, so AZERTY's M key is VK_M as the window
  procedure had it, PL9a), elsewhere what they type (SDL's Latin letters on every layout); tools/input_check checks
  the codes and the control characters against every layout the machine has (build checks). The key flags as
  Windows' message data (Alt, repeat, release; sign-extended), SystemKeyDown / Up with Alt held but not Ctrl, and
  F10. Characters: SDL's text as UTF-16 units, each carrying the last key's flags (the interface drops the console
  key's character by its scan code), and the control characters TranslateMessage made (Backspace, Tab, Enter, Escape;
  with Ctrl the active layout's on Windows, `platform::LayoutControlCharacter`: Ctrl+[ 0x1B, Ctrl+] 0x1D, Ctrl+\\ 0x1C
  on a US layout, none for those keys on a Russian one; elsewhere Ctrl+letter 1..26, Ctrl+Escape 0x1B, Ctrl+Backspace
  0x7F, Ctrl+Enter 0x0A; none with Alt).
- **The mouse**: MK_ bits (the buttons held, Shift, Ctrl), positions in the window, a second click (clicks even) as
  LeftDouble / RightDouble, the wheel 120 a notch at the screen position. `MouseDevice::Read` gives the motion events'
  sum since the last read, the wheel and the buttons (DIMOUSESTATE's layout).
- **The cursor**: the game's pictures as SDL colour cursors (`MakeCursor`: `SDL_CreateColorCursor` on an RGBA
  surface; the renderer makes them, docs/notes/renderer.md section 9, R40), shown with `SDL_SetCursor` +
  `SDL_ShowCursor`, hidden with `SDL_HideCursor` (null shape). The position is `SDL_GetGlobalMouseState`, placed with
  `SDL_WarpMouseGlobal`. `HoldCursor`: the window in relative mode with a one-pixel mouse rectangle at the point (the
  hints keep the cursor visible there, not centred, the motion scaled as the pointer's; the motion's fractions are
  carried to the next event); `SetCursorPosition` lets it go before it warps, since SDL puts the pointer back where its
  motion went when relative mode ends.
- **The clocks**: both are `SDL_GetTicks` plus a day (`kClockStart`), so no read is near 0 (the game keeps 0 as
  "never" in times it compares with them, and the original ran on machines that had been up for a while).
  `Sleep(0)` yields (`std::this_thread::yield`); SDL_Delay(0) would return at once.
- **Threads**: `std::thread`; the sound's stream thread makes itself time-critical as it starts
  (`SDL_SetCurrentThreadPriority`, SetThreadPriority's 15). Its only wait was the close's, now a join.
- **The message box**: `SDL_ShowMessageBox`, owned by the game window when the caller names it; closed without a
  button, an OK / Cancel box answers Cancel.

### How the events flow (the window-procedure path)

The harness's builds (above); SDL's backend gives the game the same Events from its own events.

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
  `Event::native` is the message's id and values (`Event::window` its window), for `DefaultHandling` (DefWindowProcA)
  and `DispatchEvent` only. The
  default handling goes to the message's own window: the original's loop passed every message it took to the game
  window's procedure as the game window's, which differs only for a message posted to another window of the thread
  or to none (none the game handles).
- Unknown messages are `Other`; the QueryCancelAutoPlay message (registered the first time a message is not one of
  the others) is `AutoPlay`. The game's own wheel message 0xCC54 (a fixed number in the registered messages' range,
  which nothing sends) is gone.
- The frame scaled into the window: present_window.cpp maps mouse events (`MouseToGame`) and gives the cursor calls
  its mapping with `platform::MapCursorPoints` at each present (it replaced the GetCursorPos / SetCursorPos slots
  before); every game call of the cursor's position goes through platform.h's cursor calls.
- Key codes stay the virtual-key values, as `platform::kKey...` constants (platform.h's key codes; the bindings file and the
  options keep the values); a port gives its key events the same values.

## Graphics

`src/platform/graphics.h` (namespace `platform::graphics`, MAC3f) is what the renderer needs of a graphics backend and
no more; sokol_gfx draws everything, so a backend is small. No backend type crosses it: sokol's own structs, our
`Rect`, fixed-width integers.

- The device: `PrepareInBackground` (made on a thread while the game starts up), `Start` (sokol's environment),
  `Stop` (after `sg_shutdown`: everything the backend made released), `Error`, `MaxSamples` (the frame target's
  multisampling) and `Lost` (a loss in words: a failed present or resize, or the device's own removed reason; null
  while the device works). The renderer asks `Lost` at the end of every presented frame and ends the game on a loss
  (docs/notes/renderer.md, "No device-loss recovery").
- The window: `BeginDrawable` gives sokol the window's drawable for the frame (`PresentMode`: the vertical blank and
  tearing, docs/notes/renderer.md section 15a), `EndDrawable` presents it after `sg_commit`. The window's size in
  pixels is platform.h's `WindowPixelSize` (SDL: `SDL_GetWindowSizeInPixels`, false while minimised; Windows: the
  client rectangle).
- Viewports and scissor rectangles go through `ApplyViewport` / `ApplyScissorRect`, from the target's top left on
  every backend (sokol's own flip is off on OpenGL, whose clip control already counts rows from the top).
- Buffers: `UpdateBuffer` writes a range of a buffer and keeps the rest (render_buffers.cpp's `GpuPool`: terrain
  pages and meshes kept on the GPU, only their changed ranges sent). It is the one transfer sokol cannot do (it writes
  a buffer only whole, once a frame); the buffer is a sokol one, made with `usage.write_unsealed` and sealed at once.
  Every other transfer is sokol's own or a draw: an image's pixels go in as an image of their own drawn texel for
  texel, a copy is a draw, the levels below are drawn one from the other (docs/notes/renderer.md section 7).
- Diagnostics: `Readback` (a region of an image or of the drawable, mapped until the next read; the backend's
  `Staging` behind it) and `GpuTimer` (timestamps read frames later without waiting; the backend's `Queries` behind
  it). Both are typed pimpls: render code holds no backend object.
- `platform::FloatingPointGuard` (platform.h) puts the x87 control word (x86) or MXCSR's control bits (x64) back
  after the driver changed it; the renderer holds one around every call into sokol that may reach the driver.
- OpenGL (graphics_gl.cpp, Linux): an SDL GL 4.5 core context, made on a hidden window of its own at `Start` and
  moved onto the game's window (made with SDL's GL flag) at its first frame. `glClipControl(GL_UPPER_LEFT,
  GL_ZERO_TO_ONE)` gives every target Direct3D's rows, depth range and facing, so the shaders, sokol-shdc's GLSL 4.30
  output, need no flip; the window's drawable is a framebuffer of ours, blitted upside down into the window's at
  `EndDrawable`. `UpdateBuffer` is `glNamedBufferSubData`, `Readback` `glGetTextureSubImage`, `GpuTimer` GL's
  timestamp queries; no lost context is looked for.
- What only Direct3D has is in `d3d11/graphics_d3d11.h`, never included by game code: the vertical blank rules
  tools/perf_check checks on real chains (`TearingSupported`, `ChainFlagsFor`, `SyncIntervalFor`, `PresentFlagsFor`).
  No render file calls Direct3D (MAC3g, MAC3h).

## Left for the next areas

- The renderer's own timings are `std::chrono::steady_clock` (render_internal.h's `Ticks` / `kMsPerTick`), not the
  game's clocks; the log (src/platform/log.cpp) is standard C++ too (`std::mutex`, `std::ofstream`, the time of day
  through lib's `FormatLocalTime`).
- The renderer's own threads (render.cpp) stay its own.

## Plain C++ in place of the system

What needed no system at all became standard C++ (PL7), each checked equal to the call it replaced:

- **Asset text** (asset_text.cpp): the code page's upper half as a table (1250, 1251 and 1252, taken from
  MultiByteToWideChar: every byte one character, the undefined ones the C1 control of their number) and a strict UTF-8
  decoder (MB_ERR_INVALID_CHARS's rules: shortest form, no surrogates, nothing past U+10FFFF, nothing cut short); a
  character the code page lacks becomes '?', one past U+FFFF "??" (WideCharToMultiByte's two units). A scratch sweep
  against the old MultiByteToWideChar / WideCharToMultiByte code (every 1-3 byte string, every code point, 3 million
  random strings with colour escapes: 10.2 million cases) found no difference. `text::CodePoints` (the interface's
  drawing and measuring) decodes UTF-8 the same way; text that is not UTF-8 is taken in the asset code page (before:
  the system's ANSI code page, the same on the machines the data is made for).
- **Formatting**: the script errors and DXException's texts are `std::vsnprintf` into the same 1 KB buffer (wvsprintfA
  before; the formats use only %s and %d).
- **The titles' twips** (ui_scrolling_surface.cpp): MulDiv's rounding (half away from zero, -1 for a zero divisor or
  an overflow) on 64 bits; equal to USER32's on 20 million cases but a divisor of INT_MIN, which USER32 mishandles (the
  divisor is a positive DPI).
- **The settings** (settings.h): a plain ini of our own read and written through typed accessors (GetInt / GetBool /
  GetFloat / GetString and their setters), no REG_* / ERROR_* codes. Nothing reads or writes the registry (the game's
  CWinRegistry has no counterpart).
- **Bitmaps** (dib.h): `DibFileHeader`, `DibHeader`, `DibColour` with the .bmp layout (14, 40 and 4 bytes) in place of
  wingdi.h's structs.
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

`lib/rect.h` (`rects::Set`, `SetEmpty`, `Copy`, `IsEmpty`, `Contains`, `Offset`, `Intersect`, `Union`) is plain integer
code over `RECT` / `POINT` with USER32's rules, so the UI and figure files no longer call SetRect, OffsetRect,
IntersectRect ... (not an area of the layer: no system call is left). An empty rectangle has right <= left or
bottom <= top; `Intersect` and `Union` write the all-zero rectangle when nothing is left and return false (`Union`
ignores an empty operand); `Contains` excludes the right and bottom edges; `Set`, `Offset` and `Copy` do not
normalise. bink_video.cpp's two copies are `rects::Copy` too. The edge rules were compared with USER32 on 56
million edge and random rectangle pairs (H33h's scratch test); `std::min` / `std::max` are written `(std::min)` in the
header because windows.h defines the macros.

## What the game depends on (2026-10-09 estimate, kept for scale)

| Area | Original dependency | Note |
|---|---|---|
| Window, messages, cursor, input | USER32 (151 imports), DINPUT (1) | SDL3 since PL9 (the harness's builds: the window procedure) |
| 2D text and surfaces | GDI32 (83) | the native interface (P track) replaces GDI |
| Rendering | DDRAW, D3D7 through COM | isolated: sokol_gfx behind the DDraw7 / D3D7 boundary |
| Sound / video | Miles mss32 (47), DSOUND (1); Bink (12) | none since PL6: SDL3's audio device (miniaudio until PL9), our Bink 1 decoder |
| Network | WSOCK32 (22, by ordinal) | BSD sockets map almost 1:1 (PL5) |
| Files, threads, time | KERNEL32 (159), WINMM (5), ADVAPI32 (11) | PL3, PL4; time done here |
