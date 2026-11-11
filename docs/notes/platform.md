# Platform layer

The game's code reaches the operating system through one header, `src/platform/platform.h` (namespace `platform`, one
section per area), that carries no Windows SDK type (UTF-8 `std::string` / `const char*`, fixed-width integers, our own
enums and structs, a native window as an opaque `void*`) and names concepts, not Windows calls (the clocks by use, the
results as values), and one implementation file per area per backend. SDL3
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
| `src/platform/sdl/sdl_keys.cpp` | the keys' codes of every build on SDL (the harness's too) |
| `src/platform/sdl/sdl_input.cpp` | SDL's keyboard and mouse events as the platform's (`sdl_input::Translator`): the game's backend and the harness's recording of real play (harness.md, "Recorded play") take the same translation |
| `tests/verify/platform/` | the harness's platform backend: harness_platform.cpp (the window, events, clocks, cursor, mouse and clipboard on SDL, in place of platform_sdl.cpp), game_answers.cpp (the seams, in place of seams.cpp) and game_sockets.cpp (the sockets area, in place of the system's sockets.cpp) |
| `src/platform/files.cpp` | the folder calls (create, remove, rename, copy, exists, free space) on std::filesystem, every system, each path through `seams.h` |
| `src/platform/seams.h`, `seams.cpp` | the questions the backends ask that a harness run answers itself (below, "The harness's answers"): `SystemPath(path, use)`, `SystemSearches(pattern)`, the file write watch (`WriteFailsAsFull`, `WriteFailed`), `IsolatedRun()`; seams.cpp is the game's answers (the path itself or the mod's map, no watch, not isolated); also each backend's `NativePath` |
| `src/platform/path_layers.h`, `.cpp` | the layered path map the game's mod and the harness's overlay share (mods.md), the mod (`SwitchMod`); each backend's one-folder `SearchFolder` |
| `src/platform/zip_folders.h`, `.cpp` | a .zip file read as a folder (mods.md, "Zip mods"; miniz): every read of a path through a zip (File, MappedFile, ReadWholeFile, the folder search, PathExists / IsFile, the path map's lookups) asks it before the system; read only |
| `src/platform/win32/*.cpp` | the Windows remainder, one file per area, in every build: text.cpp (UTF-8 to the wide calls, `text::Wide` / `Narrow`), paths.cpp, machine.cpp, files.cpp (whole files, folder searches, File, MappedFile), other_game.cpp (the one-instance check), keyboard.cpp (key names, layouts, sdl_keys' layout answers), sockets.cpp |
| `src/platform/posix/*.cpp` | the remainder on Linux and macOS, the same areas: paths.cpp, machine.cpp, files.cpp (files, mappings and folder searches with Windows' case rules), other_game.cpp (the one-instance lock), keyboard.cpp (key names and layout answers from SDL's keymap), sockets.cpp |
| `src/platform/win32/platform_win32.h`, `src/platform/posix/platform_posix.h` | each backend's own declarations for its files: Windows' `text::Wide` / `Narrow` (the harness's Windows parts use them too); POSIX's `posix::GamePath` |

- CMakeLists.txt's ei_platform builds src/platform/sdl/*.cpp, files.cpp, seams.cpp and src/platform/win32/*.cpp or
  (Linux, macOS) src/platform/posix/*.cpp (with EI_HARNESS but platform_sdl.cpp, the system's sockets.cpp and
  seams.cpp: EI_GAME_CALLS, EI_SDL_CALLS).
  The sound and the movies are not the platform's:
  src/audio and src/video, one implementation each (sound.md). platform.h carries no backend's type. Still Windows'
  values, on purpose: `platform::Key`'s values are the virtual-key codes (Windows' layouts answer in them, the harness's
  recordings keep them, a layout's unnamed codes such as VK_OEM_8 pass through; nothing in the game reads the numbers,
  so other values would need a two-way table at those three places and a rule for the unnamed ones, for no change in
  behaviour) and scan codes are set 1 (the bindings file and the interface's key tables are indexed by them); SDL's
  backend maps its keys to both.
- Inside `namespace platform` an unqualified call of a Windows function finds the platform function of that name first
  (`Sleep`, `ScreenToClient`, `ClientToScreen`, `SetCursor`, `ActivateKeyboardLayout`): the backend calls
  the system as `::X`.

## The rule and its check

- Game code (`src/game`, `src/lib`, `src/standalone`) includes `platform/platform.h` and calls `platform::X`. A new
  call of the system goes into an area of platform.h and its section of the backend, not into the game file.
- The win32 implementation calls the system directly, the wide (UTF-16) functions where text crosses (paths, key
  names; `text::Wide` / `Narrow`). What a harness run answers itself ("The harness's answers" below), which the
  goldens depend on, goes through a seam (seams.h) or is a whole area file the harness replaces (the sockets); the
  harness never replaces half of an area or a class. Every file and folder call takes its path through `SystemPath`
  ("Files and the harness's overlay" below).
- platform.h names concepts, not Windows calls: a backend-only function goes in the backend's header (the layout
  answers SDL's backend asks are sdl_keys.h's), results are values (`std::optional<Size>`, `Point`, `std::string`)
  rather than out-pointers. Windows' encodings stay only where a file or wire format keeps them (`Key`'s values,
  above; an `Address`'s 16 bytes).
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
| Clocks | `FrameClock()` (the original's timeGetTime: the loop, animations, the interface, sound, movies), `CoarseClock()` (its GetTickCount: network ids, the sound cache's ages, the menu's lightning), `Sleep`, `SecondsSince1970()` (a save's date: lib/calendar.h turns it into the local date and text) | SDL (`std::chrono::system_clock` for the date); harness builds: the harness's clocks and fixed date (harness_clock.h) |
| The message box | `ShowMessageBox(owner, text, caption, buttons, icon)`, the owner a `NativeWindow` | SDL; harness builds: the harness's answer |
| Paths | `ProgramPath()` (the system's separators), `CurrentFolder()`, `SetCurrentFolder`, `UserDataFolder()`, `DefaultGameFolder()` and `DefaultOutputFolder()` and `DefaultSettingsFolder()` (the program's folder; on macOS the game folder found or chosen, ~/Library/Logs/ei-rebuild and ~/Library/Application Support/ei-rebuild: macos.md, "The game folder and the log"), `ArgumentsAfterSeparator` | GetModuleFileNameW, GetCurrentDirectoryW, SetCurrentDirectoryW, GetEnvironmentVariableW |
| The machine | `PrimaryDesktopSize()` (none when unknown), `EnvironmentVariable` | direct |
| Files and folders | `CreateFolder`, `RemoveFolder`, `RemoveFile`, `RenameFile`, `CopyFileOver`, `PathExists`, `IsFile`, `FreeDiskSpace` (files.cpp, std::filesystem); `FolderEntries` (a folder search's `FolderEntry`s: name, path, size, folder); `ReadWholeFile` / `WriteWholeFile`; `File` (`FileMode` Read / Create / WriteExisting; also text lines: `WriteLine`; positions `std::uint32_t`, a seek's offset `std::int32_t`; its handle a `std::intptr_t`, -1 for none: a HANDLE or a descriptor), `MappedFile` (read only), `FileError` | every path through `SystemPath` (the harness's overlay, below); CreateFileW, FindFirstFileW, CreateFileMappingW direct; each write asks the write watch (seams.h: the harness's failed-write watch and its "diskfull"); a path through a .zip is read from the zip (zip_folders.h) |
| The window and its events | `StartWindowSystem(name)`, the game window (`CreateGameWindow`, `ShowGameWindow`), `Event`s (keys with modifiers, UTF-8 text, a mouse button with its click count, the wheel in notches, the focus, the close request), the event queue (`EventWaits`, `NextEvent()` (waits; a Quit event at the quit request), `TakeEvent()` (none when none waits), `PostQuit`), `SetModalLoopStep` (a function called while the system's modal move / size loop holds the main loop: an SDL event watch on live-resize redraws; the harness's backend has none), screen / client points (`ScreenToClient`, `ClientToScreen` return the point), `CoverMonitor` (the old place and the monitor, none when it did not move), `SetFullScreen`, `WindowPixelSize` (none while minimised), `SetClientSize`, `DisplaySizes` (the display's modes' sizes for the resolution row) | SDL; harness builds: the harness's backend on SDL (harness_platform.cpp) |
| The other game | `BringOtherInstanceForward(name, title)`: another copy runs, its window brought forward | Windows in every build: FindWindowW and the foreground calls; POSIX a lock file; an isolated run (seams.h) finds none |
| The cursor | the game's shapes (`MakeCursor`, `FreeCursor`, `SetCursor`), the hold (`HoldCursor(point)`, `ReleaseCursor()`), its confinement to the window (`ConfineCursor`), its position in game points (`CursorPosition()`, none when unknown; `MapCursorPoints` for the scaled frame) | SDL; harness builds: the same SDL cursors with their pixels kept for the checks, the harness's own mouse |
| The keyboard | `KeyName(scan)` (UTF-8, empty for none), `ActivateKeyboardLayout(language, fallback)`, `IsLetter(character)` (the message box's edit field); the layout answers SDL's backend asks (`sdl_keys::LayoutKey`, `LayoutControlCharacter`) in the same file | Windows in every build (SDL has no key names in the layout's language, no layout choice): GetKeyNameTextW, the keyboard layout calls, IsCharAlphaW |
| The clipboard | `CopyText`, `ClipboardText` (Rebirth's console) | SDL; harness builds: direct |
| Threads | none of the platform's: the threads are `std::thread`, their waits `std::condition_variable` or a join, locks `std::mutex` / `std::recursive_mutex` | the C++ library |
| Key codes | `enum class Key` (`Key::Escape`, `Key::Return`, `Key::A` ..., `KeyAfter`): a key as the active layout makes it, its value the virtual-key code | none |
| The mouse | `TakeMouseMotion()`: the relative motion since the last call (the buttons and the wheel are events) | SDL's motion events; harness builds: the harness's own motion (harness_platform.cpp) |
| Sockets | IPv4 UDP / TCP sockets (`Socket`, pointer-sized), `Address` (sockaddr_in's 16 bytes), `Select` on `SocketGroup`s, `HostName()` and `LookUp(name)` (none for an unknown host), `LastError()` as our `Error`; Winsock starts with the first socket call (1.1, never cleaned up). Byte order and `ParseIpv4` (inet_addr) are lib/ipv4.h's | Winsock (ws2_32), one file; the harness's builds take their own whole sockets area (game_sockets.cpp: binds and datagrams on 127.0.0.1, a pair's network) |

The sockets mirror BSD's one to one, each failing as Winsock's does, because the game relies on the failures:
`Select` leaves only the ready sockets in its sets, so MasterLinkTick's failure check after a select that found the
socket not ready asks with an empty set, which always fails (a refused master connect never closes the link there);
NetAddress::Format looked the address's name up with the whole 16-byte address as the bytes, so it never found one
and always gave the dotted number (now `NetAddress::ToString`);
an `Address` whose family is not 2 fails in the system. The second master (net_second_master.cpp, EI_Plugin's) goes
through the same platform calls (the harness's answers touch only its binds and datagrams); its look-up thread, wait and times are standard C++
(`std::thread`, a future, `steady_clock`): the real clock, never the harness's.

The two clocks stay two: the original reads `timeGetTime` (now `FrameClock`) and `GetTickCount` (`CoarseClock`) in
different places, and the harness drives each separately, so a call keeps the clock it had. QueryPerformanceCounter is not used for them: it would
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
with FILE_ATTRIBUTE_NORMAL, and takes no buffering hints. Most of the original's opens were exclusive; an open that
sharing refused would now succeed, but none happens: a check over both suites found only read opens of a file already
open for reading (the .res archives and maps/zq.mq), which were shared then too.

**One path seam** (src/platform/seams.h): every file and folder call of every backend takes its path through
`platform::SystemPath(path, use)` and then the backend's `NativePath`. `PathUse` says what the call does with it:
`Read` (opens, maps, searches, exists, free space), `Write` (creates, truncates, removes, a rename's target) or
`Update` (a change of a file that is there: WriteExisting, a rename's source), or `Layer` (ReadLayerFile: one layer's
own file read as it is, never mapped; the harness traces it: mods.md, "Records as TOML"). The game's builds link
seams.cpp's (the path itself, or with a mod the mod's layered map: mods.md); the harness's builds link
game_answers.cpp's (harness.cpp's MapPath), which maps the path into the run's overlay, the same map
(src/platform/path_layers.h) with the overlay as its top layer:

- `Read`: the top layer that has the file (the run's copy of a save or install file when it has one, else the file itself); config\movie.ini
  maps to "" (missing: no startup movies).
- `Write` / `Update`: the top layer (the overlay's copy; `Update` copies the install's file there first, so a rename moves
  the copy, never the install's file); the save trace sees the write.

A folder search asks `SystemSearches(pattern)` instead: the searches of every layer that has the folder, top first,
which FolderEntries (files.cpp) merges, a name an upper layer has hiding the same name below (mods.md, "The layered
path map"); without layers one search, the system's own order.

The seam is plain C++ (std::string_view in, std::string out), so the harness on Linux maps paths with the same
function. Rebirth's look-ups (IsFile, FolderEntries) and the settings file (ReadWholeFile / WriteWholeFile next to the
exe or in the user's data folder) pass the seam too; their paths are outside the saves and the install's overlaid
files, so the map leaves them as they are. Nothing of the harness sits below the seam: a write still under an install
after the mapping is refused there (standalone-verification.md, "No run writes into an install").

No file outside src/platform calls Windows.

### The harness's answers

The harness answers the game's calls in its own builds, through named seams or whole area files, never half an area
or a class: CMakeLists.txt leaves src/platform/seams.cpp, src/platform/<system>/sockets.cpp and platform_sdl.cpp out
of them (EI_GAME_CALLS, EI_SDL_CALLS), and tests/verify/platform defines the same functions, one file each for both
systems:

- game_answers.cpp, the seams (seams.h): `SystemPath` (above: harness.cpp's MapPath); the write watch, which each
  backend's `File::Write` asks (`WriteFailsAsFull`: "diskfull" fails a file's writes as on a full disk;
  `WriteFailed`: a failed write fails the run; harness_crash.h); `IsolatedRun()`, true in a harness run, so
  `BringOtherInstanceForward` finds none.
- game_sockets.cpp, the sockets area whole: `net::Bind`, `SendTo`, `ReceiveFrom`, `Close`, `Connect`, `Send`, `Receive`
  and `Select` answered (binds and datagrams on 127.0.0.1, a pair's network; harness_net.h, harness_pair.h); `Open`,
  the socket options, `LastError`, `HostName` and `LookUp` as the game's own on each system.
- harness_platform.cpp, SDL's areas: the clocks, sleeps and date are harness_clock.h's; the message box is the
  harness's answer.

Each answer is the game's while the harness is not running (a verify build started without EI_HARNESS). The harness's
own system calls (threads, clocks, files, faults, stacks, a pair's shared memory, the window's concealment) are
tests/verify/system/harness_system.h's, system_win32.cpp and system_posix.cpp. The harness hooks nothing of the
system.

## Window and events

### SDL3 and the harness

A framework that owns the loop and the swap chain does not fit (the game has nested loops, our renderer its own swap
chain, the harness its own input). SDL3 does: it pumps from the game's own loops (`SDL_PollEvent` inside `EventWaits` /
`NextEvent` / `TakeEvent`, `SDL_WaitEvent` while NextEvent waits), and the renderer keeps its device and swap chain on the
window (gpu.h's `ClaimWindow`; the NativeWindow the game holds is `SDL_PROP_WINDOW_WIN32_HWND_POINTER`).

- **Linkage: static.** third_party/SDL (3.4.18, zlib licence) is built by CMakeLists.txt as SDL3-static with only
  video, audio and the GPU API on Direct3D 12 and Vulkan (no 2D renderer, camera, joystick, haptic, HID, sensor, power,
  dialog, OpenGL), before the game's compile options, into ei-rebuild.exe: the exe stays one file dropped into a game
  folder. Its sources compile without a warning under its own flags.
- **The harness has its own platform backend on SDL** (tests/verify/harness_platform.cpp), built in its builds in
  place of the game's platform_sdl.cpp (the key tables, sdl_keys.cpp, are shared): an SDL window
  shown without activation, every system event SDL takes dropped (the real keyboard and mouse never reach a harness
  game; counted for the report), the harness's events queued for the game's loops, its steps run before EventWaits /
  TakeEvent look (where its runs took posted input; false holds the events for the close-all pass), a key's character
  queued by NextEvent as the harness's TranslateMessage posted it, the focus changes only the harness's focus steps
  make (queued ahead of the waiting input, as Windows told the game at once), its message box answers, and the harness's clocks, date and mouse position (harness_clock.h's virtual
  and held clocks and fixed date, harness.cpp's own mouse). The backend is SDL and
  the harness's own system layer (tests/verify/system), the same on both systems. Its steps queue the
  platform's events (harness_input.h: a key with its scan code, Alt's own key holding Alt, a character as UTF-8 text
  with its key's scan code, a double click's second press with two clicks, the wheel's turn), as the game's own
  backend makes them. SDL's translation of real
  input is checked by tools/input_check against Windows' layouts. A live recording (`recordinput live`, harness.md
  "Recorded play") takes the real keyboard and mouse through that translation (sdl_input.h) instead of dropping them,
  in a window shown and activated, its cursor held as the game's backend holds it.
- What stays Windows in every build: the other game window (FindWindowW on the class name, which SDL_RegisterApp
  gives SDL's window class: "EIGAME"), the key names and keyboard layouts, the paths (SDL's pref path is the roaming
  folder and creates it, its base path is the folder, not the file: the settings file would move), the
  files and sockets. Their POSIX versions (src/platform/posix): the program's path from `/proc/self/exe`, the user data folder from SDL's pref path, the one-instance check
  as an advisory `flock` on a lock file named after the name and title (another process's window is not raised), the
  layout's key codes, Ctrl characters and key names from SDL's keymap (`SDL_GetKeyFromScancode`: what the key types,
  read as Windows' layouts give theirs; linux.md, "What differs from Windows"), `ActivateKeyboardLayout` a no-op
  (text follows the system's layout), `IsLetter` by `iswalpha_l`.

What SDL's backend does (platform_sdl.cpp):

- **The window**: hidden until ShowGameWindow, its client area the game's size. Full screen a borderless SDL window at
  the screen's top left, which `CoverMonitor` moves and sizes over its display (no display mode change, no SDL full
  screen). Windowed a decorated, resizable window centred in the primary display's usable bounds, its client area made
  smaller when it and the borders would not fit there (the harness's backend places its window the same way but never makes it smaller: harness.md, "The game window's size"). `SetClientSize` (a resolution picked in the options, windowed) sizes
  and places it the same way in the usable bounds of the display it is on (restored first when maximised, in points on
  macOS) and waits for the system (`SDL_SyncWindow`). `SetFullScreen` (the options' display mode) switches between the
  two: borderless and unsizeable (the next present's `CoverMonitor` covers the display) or decorated and resizable (the
  caller then sizes it with `SetClientSize`); on macOS `SDL_SetWindowFullscreen`, the desktop full screen
  `CreateGameWindow` uses there. `DisplaySizes`: `SDL_GetFullscreenDisplayModes` of the window's display, each
  mode's size in pixels (width times `pixel_density`), `platform::ChooseSizes` (display_sizes.h: deduped, narrowest
  first, none under 800x600 or above `SDL_GetDesktopDisplayMode`'s size); the harness's backend lists 19 fixed sizes. Hints:
  Alt+F4 is the backend's own (`SDL_HINT_WINDOWS_CLOSE_ON_ALT_F4` 0: SDL would send the close and the key both, and
  only on Windows), closing the window is a request
  (`SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE` 0), mouse events only over the window (`SDL_HINT_MOUSE_AUTO_CAPTURE` 0, as Windows
  gave them). SDL makes the process per-monitor DPI aware: its coordinates are pixels.
- **Every event waits in the queue** for the game's loops: focus gained / lost, minimised, restored become `Focus`
  (only on a change of active / minimised), the close request (and SDL's quit) `CloseRequested`, and so is Alt+F4's
  press (Alt without Ctrl, `sdl_input::IsCloseKey`, on every system; no key event for it, its release still comes; the
  harness's backend and its key steps do the same, a live recording ends on it as on the window's close). Alt+Tab is
  the system's switcher: the game sees the focus it took. SDL keeps the cursor
  set over its window and the screen saver off itself; exposures are not passed on (the renderer presents on its own).
- **Keys** (sdl_keys.cpp): the `Key` and the set-1 scan code (0x100 extended) from the key's place (a table of US
  positions); the keys a layout moves (the digit row, the letters, the main block's punctuation) take the active
  layout's key (`sdl_keys::LayoutKey`: Windows' MapVirtualKeyExW, so AZERTY's M key is the M key; on Linux
  and macOS `sdl_keys::KeyOfTyped` of what the key types in SDL's keymap), else the US place's; tools/input_check checks the codes
  and the control characters against every layout the machine has (a build check), and on Linux
  (input_check_linux.cpp, ctest's `port_input`) codes, Ctrl characters and names on X11's us, ru, fr and de layouts. A key event carries SDL's repeat and Shift, Ctrl and Alt. Text: SDL's typed
  text, UTF-8, with the scan code of the last key pressed (the interface drops the console key's character by it), and
  the control characters a key types, which SDL's text leaves out (Backspace, Tab, Enter, Escape; with Ctrl the active
  layout's, `sdl_keys::LayoutControlCharacter`: Ctrl+[ 0x1B, Ctrl+] 0x1D, Ctrl+\\ 0x1C on a US layout, none
  for those keys on a Russian one; otherwise Ctrl+letter 1..26, Ctrl+Escape 0x1B, Ctrl+Backspace 0x7F, Ctrl+Enter 0x0A;
  none with Alt).
- **The mouse**: positions in the window's pixels, a button (left, right, middle, the side buttons) with SDL's click
  count, the wheel in notches (`SDL_MOUSEWHEEL_FLIPPED` turned back). `TakeMouseMotion` gives the motion events' sum
  since the last call.
- **The cursor**: the game's pictures as SDL colour cursors (`MakeCursor`: `SDL_CreateColorCursor` on an RGBA
  surface; the renderer makes them, docs/notes/renderer.md section 9), shown with `SDL_SetCursor` +
  `SDL_ShowCursor`, hidden with `SDL_HideCursor` (null shape). The position is `SDL_GetGlobalMouseState`, placed with
  `SDL_WarpMouseGlobal`. `HoldCursor`: the window in relative mode with a one-pixel mouse rectangle at the point (the
  hints keep the cursor visible there, not centred, the motion scaled as the pointer's; the motion's fractions are
  carried to the next event); `ConfineCursor`: `SDL_SetWindowMouseGrab` (the harness's backend only logs the request; the game
  asks every main loop pass and at the focus's changes, when the answer changed, app/app.cpp: there is no option,
  the cursor is confined while the window is active and the camera's edge pan is available); `SetCursorPosition` lets it go before it warps, since SDL puts the pointer back where its
  motion went when relative mode ends; `ReleaseCursor` lets a held cursor go.
- **The clocks**: both are `SDL_GetTicks` plus a day (`kClockStart`), so no read is near 0 (the game keeps 0 as
  "never" in times it compares with them, and the original ran on machines that had been up for a while).
  `Sleep(0)` yields (`std::this_thread::yield`); SDL_Delay(0) would return at once.
- **The message box**: `SDL_ShowMessageBox`, owned by the game window when the caller names it; closed without a
  button, an OK / Cancel box answers Cancel.

### How the events flow

- `platform::StartWindowSystem("EIGAME")` (app/main_loop.cpp, before the one-instance check). The game's loop (app/main_loop.cpp
  RunMainLoop) asks `EventWaits`, takes the next event with `NextEvent` and passes it to `app::DispatchInput` (the
  interface; the focus and the close request go on to `app::HandleWindowEvent` without the interface's close-all) or
  `app::HandleWindowEvent`. `PumpMessages` takes events with `TakeEvent` and gives them to HandleWindowEvent (input
  dropped); the movie loop takes Escape and Space and gives the rest to HandleWindowEvent.
- app.cpp's dispatcher hands the interface an event's own data: a key's code, scan code and repeat, each character of
  a Text event with its scan code, a press with an even click count as a double click, the mouse's position, the
  wheel's turn signed at 120 a notch. A close request (the window's, Alt+F4) posts the quit in every loop, the loading
  screens' PumpMessages and the movie loop included; a quit taken there stays posted for the main loop.
- **Mouse positions in game points.** The game takes its mouse positions as screen coordinates of a window at 0, 0 the
  size of its resolution: the cursor position (the cursor, the global map) is read and placed without converting, mouse
  events come in the window's pixels. The window is anywhere (windowed it is
  centred and movable) and the frame may be scaled into it (render::FrameInWindow), so these are always mapped to frame
  pixels: present_window.cpp maps mouse events (`MouseToGame`) and gives the cursor calls its mapping with
  `platform::MapCursorPoints` at each present; every game call of the cursor's position goes through platform.h's cursor
  calls.
- Keys are `platform::Key` everywhere past the backend (events, the interface's KeyDown / KeyUp, the harness's steps);
  only the harness's recordings and Windows' layout answers use its value (`static_cast`), the virtual-key code. A
  layout's code without a name passes through as it is. Typed characters (Char, OnChar) are code points, compared with
  character literals (`'\b'`, `'\r'`, `'\x1B'`), not with keys.

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
- **Formatting**: the script errors are `std::vsnprintf` into a 1 KB buffer (the formats use
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
- **Dates** (lib/calendar.h): `LocalDateTime` / `FormatLocalTime` of `platform::SecondsSince1970()` by the C
  library's `localtime_s` (`localtime_r`) and `strftime`: a save's date, the log's time of day.
- **IPv4 text and byte order** (lib/ipv4.h): `ParseIpv4` is inet_addr's parser (one to four parts, decimal, octal
  or hexadecimal, the last filling the rest, whitespace ending the text, inet_addr's overflow rules), equal to Winsock
  on 6 million strings (a scratch sweep; tests/unit/ipv4.cpp keeps the edge cases); `ToNetwork` / `FromNetwork` are
  htons / ntohs.

## Rectangles

`lib/rect.h` (`Rect::IsEmpty`, `Contains`, `Offset`, `Intersection`, `Union`, `Width`, `Height`) is plain integer
code with USER32's rules in place of SetRect, OffsetRect, IntersectRect ... (not an area of the layer: no system call
is left). An empty rectangle has right <= left or bottom <= top; `Intersection` and `Union` return the all-zero
rectangle when nothing is left (`Union` ignores an empty operand); `Contains` excludes the right and bottom edges;
`Offset` and the aggregate initialisation do not normalise. Layout code builds rectangles from others with `Inset` (all
edges, dx / dy, or each edge), the `LeftStrip` / `RightStrip` / `TopStrip` / `BottomStrip` bands inside an edge and
`Rect::Sized(topLeft, size)`; none of them normalises either. The edge rules were compared with USER32 on 56 million
edge and random rectangle pairs; `std::min` / `std::max` are written `(std::min)` in the header because windows.h
defines the macros. A mouse event's position is a `Point` (the `LeftDown(int buttons, Point point)` handlers).
