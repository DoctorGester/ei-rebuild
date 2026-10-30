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
  `GetLocalTime` / `GetSystemTime`; the message hooks) and the image's UTF-8 routes (`src/platform/win32/system_text.cpp`'s
  `TEXT_ROUTE` list: paths, files, `MessageBoxA`, window text). tools/gen_iat.py's `routed()` lists them; the goldens
  depend on them. Keeping every `gameapi::X` call (moved, not removed) also keeps gameapi.h, which is in the harness's
  reference key, unchanged.
- A call that never used a slot stays direct: code from another module that read the real clock
  (net_second_master.cpp's `GetTickCount`) must not start reading the harness's.
- `tools/check_platform.py` (build.cmd, through tools/build_checks.py) lists the files under `src/` outside
  `src/platform`, `src/render` and `src/hook` (the harness's hook framework) that include `<windows.h>` or another
  system header (`<dinput.h>`, `<wingdi.h>`, `<mmsystem.h>`, `<winsock2.h>`, `<process.h>`, ...: the script lists them) or
  call `gameapi::` (comments left out). A file not in `config/platform_files.txt` fails the build; after moving a file's
  calls run `python tools/check_platform.py --update` (the list only shrinks). The dashboard shows the count
  (`platform_files`, "Files calling Windows"): 97 before the layer, 78 after the first areas, 61 after the window
  and input (PL2), 58 after the sockets (PL5), 48 after the files and the start-up (PL3, PL1b). That check looked at `<windows.h>` and
  `gameapi::` only; widened to the other system headers (PL2a) it counted 12 more files (win_types.h, win_user.h,
  dib.h, ...), and the threads, the key codes and the mouse (PL4, PL2a) took 3 off: 57; our own rectangle functions
  (below) took 31 off: 26. The last game-side users (PL7) took 17 off: 9; the sound and movie files (PL6b), the sound
  files' reads (PL7b) and lib/win_types.h (PL7a) left it: 5, the win32 entry (below) and the import layer's headers.
- The check sees a file's own includes only. lib/win_user.h (winuser.h for the DT_* constants) let ui_edit_field.cpp
  call `IsCharAlphaW` unnoticed (now `platform::IsLetter`); win_user.h is gone. lib/win_types.h (windef.h's types)
  was the last such header: 113 game files used its RECT, POINT, SIZE, DWORD, COLORREF, LONG and UINT. They use our
  own `Rect`, `Point`, `Size` (lib/rect.h: global structs of Windows' RECT, POINT and SIZE layout; the win32 side
  static_asserts the sizes where it hands one to the system) and `std::uint32_t` / `std::int32_t` (DWORD, COLORREF,
  UINT, LONG). The header is src/platform/win32/win_types.h now, for lib/win_api.h (the gameapi:: declarations) only.
- A native window crosses the layer as `void*` (`platform::NativeWindow`); game code holds no `HWND`. app.h's
  `g_mainWindow` and texture.h's device window are `platform::NativeWindow`; the renderer's window code casts it.

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
| `keyboard.h` | `KeyName(scan)`, `ActivateKeyboardLayout(language, fallback)`, `IsLetter(character)` (the message box's edit field) | the GetKeyNameTextA (UTF-8 route) and keyboard layout slots; IsCharAlphaW direct |
| `clipboard.h` | `CopyText`, `ClipboardText` (Rebirth's console) | direct |
| `thread.h` | `Thread` (created suspended, priority set, resumed), `Signal` (an auto-reset event: `Create`, `Set`, `Wait`, `Close`); locks are `std::recursive_mutex` | the SetThreadPriority / ResumeThread / CreateEventA / SetEvent / WaitForSingleObject (the harness's clocks) / CloseHandle slots; `_beginthreadex` direct |
| `keys.h` | the game's key codes (`kKeyEscape`, `kKeyReturn`, ...: the virtual-key values the bindings file keeps) | none |
| `mouse.h` | `MouseDevice`: the mouse's motion (`Open`, `SetActive`, `Read`, `Close`), failures reported to the caller's `MouseReport` | DirectInput 7 of the system's dinput.dll, loaded directly (the harness observes the object: `g_directInputObserver`) |
| `net.h` | IPv4 UDP / TCP sockets (`Socket`, pointer-sized), `Address` (sockaddr_in's 16 bytes), `Select` on `SocketGroup`s, host name and look-ups, `Start` / `Stop`, `LastError()` as our `Error` | the WSOCK32 slots (19 of its 22 imports; `WSAGetLastError` direct, game.exe does not import it) |
| `audio.h` | the output, voices (waves from memory, end callbacks), the music's double-buffered stream, the MP3 decoders | none: miniaudio (`src/platform/miniaudio`) |
| `video.h` | Bink 1 movies a frame at a time into a CPU buffer | none: our Bink 1 decoder (`src/platform/bink`) |

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
  `platform::IsFile`, `platform::FolderEntries` (the UTF-8 versions in platform/win32/system_text.cpp, called by name).
- The settings file (settings_file.cpp: next to the exe or in the user's data folder, whatever a harness run
  redirects): `ReadWholeFile` / `WriteWholeFile` (wide calls), `ProgramPath`, `UserDataFolder`, `PrimaryDesktopSize`;
  its lock is a `std::mutex`.
- The standalone's start-up (startup.cpp, standalone_main.cpp's `EnterGameFolder` and exit paths): `ProgramPath`,
  `EnvironmentVariable`, `SetCurrentFolder`, `PathExists`, `IniInt`, `ProgramInstance` and `EndProcess`
  (TerminateProcess, then `Sleep(INFINITE)` as the old code, so the call never returns). `app::GameMain` takes
  `platform::NativeInstance`.
- The win32 entry stays on the list: standalone_main.cpp, host.cpp and image_imports.cpp are the loader (the
  vectored and unhandled exception handlers, `VirtualProtect` on another module's import table, `GetModuleHandleA` /
  `GetProcAddress` of the DLLs the slots resolve to, the DPI call, the process id for the log; image_imports.cpp also
  puts the UTF-8 routes in the slots); lib/import_table.h and lib/import_prototypes.h are game.exe's import table as a
  struct, which the harness's reference key holds (game.exe's runs read the same slots: hook/original_data.cpp). A port
  has its own entry; nothing of the game's code is in them.

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
- Key codes stay the virtual-key values, as `platform::kKey...` constants (platform/keys.h; the bindings file and the
  options keep the values); a port gives its key events the same values.

## Audio and video

Miles (mss32.dll) and Bink (binkw32.dll) are gone from both builds (user 2026-10-30): no slot of theirs (nor
DSOUND's `DirectSoundEnumerateA`) is reached, so neither DLL loads, nor the `*.asi` providers Miles loaded (mp3dec.asi;
Rebirth's EI_Plugin.asi, which the image kept out anyway: its features are Rebirth flags of our code). Game code calls
`platform/audio.h`, implemented on miniaudio (`src/platform/miniaudio`, third_party/miniaudio: its device and its
dr_wav / dr_mp3 decoders), and `platform/video.h`, implemented on our own Bink 1 decoder (`src/platform/bink`). `src/platform/audio_formats.cpp` (wave headers, the length rule, the MP3 frame scan) is shared by
every backend.

### What the game reads back

| What | Read by | Kind |
|---|---|---|
| A voice's end (Miles' end-of-sample callback: `SoundSlot::finished`) | Tick frees the voice; `FindPlaying` (a unit's talking flag in unit_client_effects / unit_client_update, map_object_companion, the briefing's next phrase in ui_briefing_create); sequences start their next sound | **logic**: the goldens depend on when it comes |
| Which voices are free (follows from the ends) | AllocateVoice: a speech gets -1 with every voice taken | logic |
| A voice's byte position (`AIL_sample_position`) | kept in a stolen looping sound's deferred record, handed back at its restart | output only |
| The wave's length (`AIL_WAV_info` -> `SoundSlot::length`) | copied into deferred records | read by nothing |
| The decoded briefing voice's size | the wave cache's byte count (evictions) | memory only |
| The music stream's free half (`AIL_sample_buffer_ready`), the MP3 decoder's bytes and file end | the stream thread: `streamState`, `streamName`, `CurrentStreamName`, which UpdateMusic reads; PlayJingle / the calm music busy-wait on `streamState` | logic, but on real time (the stream thread): not deterministic before either (docs/notes/sound.md, "Non-determinism"); fights play with `music 0` |
| `AIL_minimum_sample_buffer_size` | the stream's buffer size (frames per buffer: RequestStream's switch points) | stream timing only |
| Volumes, pans, reverb, 3D | nothing: the game computes 3D itself (UpdatePositional) and hands a 2D volume and pan | output only |
| MIDI | the game ended and released a sequence it never started | gone |
| A movie's frame numbers (`FrameNum`, `LastFrameNum`, `Frames`) | the players' loops: the blocking player ends after the last frame, BinkVideo::Frame answers 0 then (the credits close once their text is done too), the loading screen's movie stops at its last frame | logic (when a player ends), not game state |
| `BinkWait` (the next frame is due) | the players' pacing | real time, not the game's clock |
| A movie's sound, volume, pixels | nothing | output only |

### The rules every backend keeps

- **Voice ends on the game's clock.** A voice's end is due its wave's length after Start (`WaveMilliseconds`: the
  data's bytes over the byte rate, IMA ADPCM's byte rate `rate * block / samplesPerBlock` truncated, then truncated
  to ms; less the start position, times the loop count; never with loop count 0), on `platform::Milliseconds`; Stop /
  Resume keep what is left. The main loop delivers the due ends with `DeliverEnds(now)` right after reading the
  frame's time (app_main.cpp TimeSinceLastPass), so the callbacks run on the main thread, in voice order, as the
  harness called Miles' callbacks on the virtual clock before (tests/verify/harness_clock.cpp, game.exe's runs). A
  backend that plays for real still times the end this way, not by the device. The rule is Miles' own: checked
  equal on every wave and briefing voice of the vanilla, Rebirth and LostInAstral archives (18993; the header fields,
  the ms and the decoded sizes), with a 32-bit tool that asks mss32.dll (scratch; a sweep against its output takes
  seconds).
- **Decoded MP3 voices** (`DecodeMp3File`, the briefing voices, `compressed` in LoadSample): a 16-bit PCM wave with a
  44-byte header at the file's rate and channels and every whole frame's samples (1152 for MPEG-1, 576 for MPEG-2 /
  2.5). That is what Miles' mp3dec.asi gave (mono stayed mono although the game asked for stereo), so the length and
  the cache's size stay the same. dr_mp3 gives fewer frames on 3968 of the installs' 9908 MP3 voices (2880..4031
  fewer: it drops the LAME / Xing info frame and the encoder's delay and padding) and the same on the rest; the
  backend pads with silence at the end. `DecodeMp3` (the music) keeps to the same count.
- **The stream** (`ReadyHalf` / `QueueHalf`): a half is free again once its frames have played. With a device the
  mixer frees it when it takes the half's last frame (no drift from the device's clock); without one (or once the
  device stopped by itself) the frames advance on `platform::Milliseconds` from the stream thread (the harness's
  other-threads clock, a real-time one), counted to the frame: the game's 7680-frame halves free 175, 349, 523 ms after
  the first was queued (the null backend's whole-ms rule gave 174, 348, 522). `MinimumHalfBytes` is 87 ms rounded up
  to 512 bytes (15360 for the game's 44.1 kHz stereo, what Miles gave through the wave mapper; through DirectSound it
  varied run to run).
- **Movies** pace on a real-time clock (std::chrono::steady_clock): the blocking player's loop reads the game's
  clock, which the harness holds while no frame runs. `Advance` after the last frame goes back to the first (the
  titles' movie loops under the text); `GoToFrame` stops at the last.

### The audio backend (miniaudio)

- The device: miniaudio's default playback device (WASAPI first on Windows), 32-bit float stereo at the game's rate
  (44100; miniaudio converts to the device's own). A device that cannot open leaves the game running as without one
  (silently, `OpenOutput` still true), so the game's logic never depends on the machine's sound hardware. Its name, the
  first voice and the first sound heard, and at the close the voices started and the mix's peak go to the debugger's
  output (`platform::DebugOutput`; DebugView shows them).
- Voices: `SetWave` decodes the whole wave with dr_wav (PCM 8 / 16 bits, IMA ADPCM) into 16-bit samples the voice
  keeps, cut to WaveInfo's count (dr_wav decodes IMA ADPCM's last block whole, past the fact chunk's count). The mixer
  plays them from the position (`SetPosition`'s bytes as frames: PCM by the block align, IMA ADPCM by its blocks) with
  the loop count, at the output's rate by straight-line interpolation (the waves at 44.1 kHz come out sample for
  sample). Volume 0..127 is linear; pan 0..127 is a balance (64 both sides full, the far side down to nothing at 0 /
  127). Volume and pan changes ramp over one device period. The end the game sees stays the clock's (above): the
  device's own end only stops the mixing.
- The cave reverb (`SetReverb(room, level, time)`; the game gives every new voice the cave's 0.3, 1.0, 3.0 s while
  the party is in a cave zone, world_client_maps.cpp, and room 0 with the level kept once it leaves one): a voice with
  room and level above 0 feeds a mono send at its level (after its volume and pan) and keeps 1 - 0.35 * level of itself
  (`kReverbDryCut`), so a full-scale voice and its tail stay under full scale. One shared Freeverb-style network
  (`Reverb`): eight damped feedback combs in parallel and four all-passes in series a side, Freeverb's tunings scaled
  by the room (0.5 + room: 0.8 for caves) and the rate, the right side 23 frames longer; each comb's feedback is the
  time's -60 dB over its own delay, the loop's low-pass at 0.1. The last SetReverb's room and time shape it (the game
  passes the same to every voice); a new room empties the lines. With nothing sent for two tails (-120 dB) and the
  longest delay it clears its lines and rests: no denormals ever, and no cost while no cave voice plays. Output only:
  the voices' ends, positions and the stream are untouched, and harness runs (no device) never mix.
- How the reverb was checked (scratch tool, A1a): a click's tail falls 60 dB in 2.6 s and steady noise's in 2.7 s of
  the 3.0 asked (the damping takes the highs sooner); the steady tail sits 11 dB under the voice; over all 3848 waves
  of the vanilla sfx.res at full volume none clips that did not clip dry (the worst peak with the reverb 1.19 times
  the dry's); dry and reverb takes of every voice play the same frames and end at the same clock time; the network
  costs about 1.4 ms of mixer time a second of audio while it runs (one core) and nothing once it rests, the same in
  every 5 s window of a 60 s tail.
- The stream: the halves are mixed straight from the caller's buffers (it writes a half only once ReadyHalf gave it)
  at each half's format (`SetStreamFormat` applies to the halves queued after it), 16 bits signed or 8 unsigned, any
  rate. More than one stream can play (a movie's sound takes one of its own).
- MP3: `OpenMp3` reads the whole file and decodes it with dr_mp3 (`DecodeMp3` in whole frames; `RewindMp3` seeks to
  the start); `DecodeMp3File` as above. Both run in every build, the harness's runs included.
- All the mixer's state (voices' sounds, streams) is under one lock; the device's thread takes it once a period. The
  voices' clock fields (ends, positions) are the main thread's, as the game calls them.
- **Harness runs open no device**: tests/verify/image/harness_audio_image.cpp calls `UseNoDevice` before main when
  EI_HARNESS is set (the harness's own trigger), so a run is silent and the same on every machine; the voices' ends and
  the stream run on the harness's clocks exactly as with a device. game.exe's runs mute Miles as before (harness.cpp's
  `MutedWaveOutOpen`). The play, release and x64 builds, and the verify build started without a scenario, make sound.
- The goldens did not move with the null backends (122 the same, 2026-10-30), nor with this one. One difference from
  the harness's timers in game.exe's runs: they keep a voice's loop count across `AIL_init_sample`, where Miles resets
  it to 1, so there a one-shot sound on a voice that once looped never ends; `ResetVoice` resets it as Miles did. No
  golden depends on it today.
- How it was checked (no ear): an offline tool that includes the backend and calls its mixer as the device would
  (scratch; it decodes the archives' entries): every wave of the vanilla, Rebirth and LostInAstral sfx / speech
  archives (9085) decodes to its header's frames, rate and channels; every MP3 voice (9908) opens in dr_mp3 with the
  scanned rate and channels and `DecodeMp3File` gives the whole frames' size; voices of each kind mixed at full volume
  have the source's length (twice the frames at 22.05 kHz, twice with two loops) and peak, the 44.1 kHz ones sample
  for sample; pan 0 / 127 leaves the other side at 0, volume 64 gives 64/127 of the peak; two Stream\*.mp3 queued in
  halves as the stream thread does come out sample for sample as dr_mp3 decodes them. A windowed run of the release
  exe (a run folder of junctions, Work path / CD-ROM Path `.\`, 45 s to the menu, closed with WM_CLOSE) logged the
  WASAPI device, the first voice and sound, and 206 voices at the close; a `sound` scenario run with a DebugView-style
  listener showed no device and no audio session opened. Goldens: 122 + 24 (Rebirth) the same.

### Movies

The backend is our own Bink 1 decoder (`src/platform/bink`): `bink_file` the container (the header, the frame index,
a frame's packets read from the file when it is decoded), `bink_video` the video, `bink_audio` the sound,
`bink_tables` the format's constant tables, `bit_reader` the bit order, `movie.cpp` video.h. It was written from the
format's descriptions (multimedia.cx: Bink Container, Bink Video, Bink Audio; Kostya Shishkov's notes), with the game's
binkw32.dll studied for what they leave out (the tables are its numbers). FFmpeg's sources were never read (the exe is
MIT and links nothing of FFmpeg's); FFmpeg ran only offline as the oracle.

- What it plays: Bink 1 revision i ("BIKi") with video flags 0, the first sound track when it is the RDFT variant.
  That is every movie the installs ship: 46 distinct files (vanilla 640x272 and 800x600, Rebirth's 1920x816 /
  1080 / 960, LostInAstral's; sound 44.1 or 22.05 kHz stereo, many without). Other revisions, alpha, grayscale and
  the scaled / interlaced modes do not open (`Open` gives null: the game logs "Can't open"); a DCT track plays
  silent (none ships, and binkw32 1.0 the game came with has no DCT decoder).
- The video: per frame the luma's size word, then the planes Y, V, U (V comes first), each from a 32-bit word of its
  own; 8x8 blocks with the bundles and prefix codes as the format describes. The frame before the first is all 0
  bytes; a motion vector is kept inside the plane. Frames as R, G, B, 255 by BT.601 limited range with RAD's 15-bit
  tables (each term truncated), a chroma sample per 2x2 pixels.
- The sound: transform frames of 2048 samples a channel at 44.1 kHz (1024 at 22.05), the channels interleaved in one
  real transform; a frame's first sixteenth is blended with the frame before's tail, each frame clipped to 16 bits
  before the blend (RAD's order). A packet gives as many samples as its header says (the last frame of the track is
  cut there). The transform is computed in double.
- Playback: `DecodeFrame` decodes in order (the last frame on to the first too: the titles loop with their sound
  going on); a jump (`GoToFrame` past the next frame, the loading screen) decodes from the keyframe at or before the
  frame, or on from the decoded one when that is nearer, without those frames' sound. `Waiting` is due the movie's
  frame time per `Advance` after `Open`, on std::chrono::steady_clock.
- The sound plays when `Open`'s `sound` and the output gives a stream (`CreateStream`): the frames' samples are queued
  as they decode (Bink's first frame carries a lead of several frames) and handed to the stream in halves of
  `MinimumHalfBytes` (87 ms) whenever `ReadyHalf` frees one (in `Waiting` and `DecodeFrame`); the rest goes once the
  last frame is decoded. Format: the track's rate and channels, 16 bits; `SetVolume` 0..32768 maps linearly to the
  stream's 0..127 (127 until set). Video and sound each keep their own clock: no resync after a stall of the game.
- Cost (x64 release, one core): a 1920x816 frame about 4 ms, an 800x600 one about 1.3 ms; the sound about 1% of
  real time.
- How it was checked (scratch tool and ffmpeg 9.0.2, gyan.dev's essentials build, both outside the repository; a sweep
  takes 2 minutes): on all 46 files every frame's YUV (ffmpeg `-pix_fmt yuv420p -f framemd5`) is identical, 16113
  frames. The sound of the 27 files with a track matches ffmpeg's s16 within 1 (about 96.7% of the samples exact:
  ffmpeg decodes in float and converts float samples to 16 bits); larger differences only inside the blended frame
  starts of loud passages (ffmpeg blends before clipping), at most 726 samples of a file; ffmpeg gives the
  last transform frame whole (up to one frame more at the end). RGB against ffmpeg's rgb0: within 2 per channel,
  mean 0.65 (swscale's own rounding). A windowed run of the release exe (a run folder of junctions without
  binkw32 / mss32, Work path / CD-ROM Path `.\`) showed the start-up movies in their colours and reached the menu;
  the movie's stream was the first sound to reach the device; closed with WM_CLOSE.
- Harness runs: the output has no device but gives streams, so a movie's sound decodes and its halves wait on the
  game's clock; movies pace on the real-time clock as before. The goldens did not move.

## Left for the next areas

- standalone_main.cpp, host.cpp and image_imports.cpp: the loader (see "Files and the direct calls"); they stay.
- The renderer's QueryPerformanceCounter / Frequency (render.cpp, render_perf.cpp, render_textures.cpp) stay its own.
- The renderer's own threads and lock (render.cpp, render_cursor.cpp) stay its own; render_cursor.cpp's lock is a
  `std::recursive_mutex` (a critical section re-enters).
- The import layer's own files (lib/import_table.h, import_prototypes.h, gameapi.h / .cpp, win_api.h) stay in
  src/lib while the harness's reference key holds them.

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
- **The settings file's codes** (settings_file.h): `ValueType` (kTypeString, kTypeDword, ... the registry's numbers,
  which the file keeps) and `Status` (kSuccess, kNotFound, kMoreData, ...) in place of REG_* / ERROR_*. Nothing reads or
  writes the registry: the options' key object is `SettingsKey` (settings_key.h, the game's CWinRegistry) on the file.
- **Bitmaps** (dib.h): `DibFileHeader`, `DibHeader`, `DibColour` with the .bmp layout (14, 40 and 4 bytes) in place of
  wingdi.h's structs.
- **Text format bits**: render/text_format.h (DrawText's DT_* values) for the interface's DrawTextIn.
- **Return addresses** (the harness's observers in random.cpp, crt.cpp, core_streams.cpp): lib/return_address.h
  declares the two compiler intrinsics without <intrin.h>.
- **Rebirth's flags** (lib/rebirth.cpp, also in ei_harness.dll): ei_rebirth.ini's [Rebirth] read by its own reader as
  GetPrivateProfileInt / String read it (the first section of the name, case ignored, trimmed keys and values, ';'
  lines, outer quotes off, an empty value the default, a sign then 0x hex (lowercase x) or decimal digits, 32-bit
  wrap); EI_REBIRTH with `getenv_s`; the file's presence with std::filesystem. Equal to the profile calls on 24000
  reads of random files (scratch sweep).
- The world's wincurs\Df0000.cur cursor (LoadCursorFromFileA) was loaded and never used: gone.

## Rectangles

`lib/rect.h` (`rects::Set`, `SetEmpty`, `Copy`, `IsEmpty`, `Contains`, `Offset`, `Intersect`, `Union`) is plain integer
code over `RECT` / `POINT` with USER32's rules, so the UI and figure files no longer call SetRect, OffsetRect,
IntersectRect ... (not an area of the layer: no system call is left). An empty rectangle has right <= left or
bottom <= top; `Intersect` and `Union` write the all-zero rectangle when nothing is left and return false (`Union`
ignores an empty operand); `Contains` excludes the right and bottom edges; `Set`, `Offset` and `Copy` do not
normalise. The harness routes none of the eight slots. bink_video.cpp's two copies are `rects::Copy` too, so
gameapi.h has no rectangle import left. The edge rules were compared with USER32 on 56
million edge and random rectangle pairs (H33h's scratch test); `std::min` / `std::max` are written `(std::min)` in the
header because windows.h defines the macros.

## What the game depends on (2026-10-09 estimate, kept for scale)

| Area | Original dependency | Note |
|---|---|---|
| Window, messages, cursor, input | USER32 (151 imports), DINPUT (1) | sokol_app planned (PL2) |
| 2D text and surfaces | GDI32 (83) | the native interface (P track) replaces GDI |
| Rendering | DDRAW, D3D7 through COM | isolated: sokol_gfx behind the DDraw7 / D3D7 boundary |
| Sound / video | Miles mss32 (47), DSOUND (1); Bink (12) | none since PL6: miniaudio, our Bink 1 decoder |
| Network | WSOCK32 (22, by ordinal) | BSD sockets map almost 1:1 (PL5) |
| Files, threads, time | KERNEL32 (159), WINMM (5), ADVAPI32 (11) | PL3, PL4; time done here |
