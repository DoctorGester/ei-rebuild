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
  and input (PL2).
- A native window crosses the layer as `void*` (`platform::NativeWindow`); game code keeps an `HWND` only where a
  shared header holds one (app.h's `g_mainWindow`, texture.h's device window; `HWND` comes from lib/win_types.h, not
  `<windows.h>`).

## Areas

| Header | What | Windows calls behind it |
|---|---|---|
| `clock.h` | `Milliseconds()` (timeGetTime), `TickCount()` (GetTickCount), `Sleep`, `LocalTime()` | the slots of timeGetTime, GetTickCount, Sleep, GetLocalTime |
| `debug.h` | `DebugOutput` | OutputDebugStringA |
| `message_box.h` | `ShowMessageBox(owner, text, caption, buttons, icon)` | the MessageBoxA slot (UTF-8 route) |
| `paths.h` | `ExePath()`, `CurrentFolder()` | the GetModuleFileNameA / GetCurrentDirectoryA slots (UTF-8 routes) |
| `system.h` | `PageSize()`, `FontSmoothing()` | GetSystemInfo; the SystemParametersInfoA slot (the harness fixes smoothing off) |
| `files.h` | `CreateFolder`, `FreeDiskSpace`, `VolumeLabel` | the CreateDirectoryA (harness redirect, UTF-8 route) and GetVolumeInformationA slots; GetDiskFreeSpaceExW direct |
| `window.h` | the window class, the game window, `Event`s, the event queue (`EventWaits`, `NextEvent`, `TakeEvent`, `DefaultHandling`, `DispatchEvent`, `PostQuit`), screen / client points, `CoverMonitor` | the window and message slots (the harness hooks CreateWindowExA, ShowWindow, PeekMessageA, FindWindowA); the screen mapping and monitor calls direct |
| `cursor.h` | the cursor's shape, its position in game points (`MapCursorPoints` for the scaled frame) | the GetCursor / SetCursor / GetCursorPos / SetCursorPos slots (the harness's own mouse) |
| `keyboard.h` | `KeyName(scan)`, `ActivateKeyboardLayout(language, fallback)` | the GetKeyNameTextA (UTF-8 route) and keyboard layout slots |
| `clipboard.h` | `CopyText`, `ClipboardText` (Rebirth's console) | direct |
| `audio.h` | the output, voices (waves from memory, end callbacks), the music's double-buffered stream, the MP3 decoders | none: `null/audio.cpp` (silent, timed); A1 adds a real one |
| `video.h` | Bink 1 movies a frame at a time into a CPU buffer | none: `null/video.cpp` (black frames, timed); A2 adds a decoder |

The two clocks stay two: the original reads `timeGetTime` and `GetTickCount` in different places, and the harness
drives each separately, so a call keeps the clock it had. sokol_time (QueryPerformanceCounter) is not used for them: it
would bypass the harness's clocks and change the replays.

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

## Audio and video

Miles (mss32.dll) and Bink (binkw32.dll) are gone from both builds (user 2026-10-30): no slot of theirs (nor
DSOUND's `DirectSoundEnumerateA`) is reached, so neither DLL loads, nor the `*.asi` providers Miles loaded (mp3dec.asi;
Rebirth's EI_Plugin.asi, which the image kept out anyway: its features are Rebirth flags of our code). Game code calls
`platform/audio.h` and `platform/video.h`; today both are the null backends in `src/platform/null`, which A1
(miniaudio) and A2 (our Bink 1 decoder) replace. `src/platform/audio_formats.cpp` (wave headers, the length rule, the
MP3 frame scan) is shared by every backend.

### What the game reads back

| What | Read by | Kind |
|---|---|---|
| A voice's end (Miles' end-of-sample callback: `SoundSlot::finished`) | Tick frees the voice; `FindPlaying` (a unit's talking flag in unit_client_speech, unit_client_sounds / update, map_object_companion, the briefing's next phrase in ui_briefing_create); sequences start their next sound | **logic**: the goldens depend on when it comes |
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
  the cache's size stay the same. A1's decoder (dr_mp3) pads or cuts to that count.
- **The stream** (`ReadyHalf` / `QueueHalf`): a half is free again once its bytes have played at the stream's format;
  the null backend plays them on `platform::Milliseconds` from the stream thread (the harness's other-threads clock, a
  real-time one). `MinimumHalfBytes` is 87 ms rounded up to 512 bytes (15360 for the game's 44.1 kHz stereo, what
  Miles gave through the wave mapper; through DirectSound it varied run to run).
- **Movies** pace on a real-time clock (std::chrono::steady_clock): the blocking player's loop reads the game's
  clock, which the harness holds while no frame runs. `Advance` after the last frame goes back to the first (the
  titles' movie loops under the text); `GoToFrame` stops at the last.

### The null backends

- Audio: the output always opens; voices keep their wave, loop count, volume, pan and position and end on time;
  the stream's halves free as they would play; the MP3 decoders count the frames (`ScanMp3`) and give silence of the
  file's decoded length. So the game runs its full sound logic (enabled, 15 voices, the stream thread) silently, on
  both builds: the x64-only "Miles not loaded" path (A3a) is gone.
- Video: a Bink 1 file opens with its size, frame count and rate from its header ("BIK", frames at +8, width +20,
  height +24, rate +28 / +32); frames come black at the movie's rate. Movies play as long as with pictures
  (Esc skips them) and the script's PlayMovie still stops the sounds first, as with Bink.
- The goldens did not move with the null backends (122 the same, 2026-10-30). One difference from the harness's
  timers in game.exe's runs: they keep a voice's loop count across `AIL_init_sample`, where Miles resets it to 1, so
  there a one-shot sound on a voice that once looped never ends; `ResetVoice` resets it as Miles did. No golden
  depends on it today.

### For A1 and A2

- A1 implements audio.h on miniaudio in a backend of its own (src/platform/miniaudio/, replacing null/audio.cpp in
  CMake's list): mix the voices' waves (PCM and IMA ADPCM through dr_wav's decoders) at their volume (0..127) and pan
  (0 left .. 127 right), loops, Stop / Resume / End; the stream's halves in order; `DecodeMp3` / `DecodeMp3File` with
  dr_mp3. Keep the timing rules above: ends from `WaveMilliseconds` on `platform::Milliseconds` and `DeliverEnds`
  (not the device's end), `DecodeMp3File`'s sample count, and the stream's halves freeing as the device plays them.
  Reverb (`SetReverb`) has no effect today either way.
- A2 implements video.h with our Bink 1 decoder (src/platform/bink/): the same header and frame numbering, frames as
  R, G, B, X into `CopyFrame`'s buffer, `Waiting` from the movie's rate on a real-time clock, the sound track (when
  `Open`'s `sound`) through A1's output, `SetVolume` 0..32768.

## Left for the next areas

- src/standalone/startup.cpp and standalone_main.cpp (`GetModuleFileNameA`, `SetCurrentDirectoryA`,
  `Sleep(INFINITE)`, `GetModuleHandleA`; these run before or beside the import table and call the system directly;
  PL1a's second half, after A3a), lib/file_io.cpp (`GetCurrentDirectoryA` / `GetFullPathNameA`, PL3), resource.cpp's
  `GetSystemTime` (the mapped file's write time, PL3).
- `GameMain(HINSTANCE, HINSTANCE, char*, int show)` keeps WinMain's types (win_types.h) while standalone start-up
  calls it (A3a's files).
- The renderer's QueryPerformanceCounter / Frequency (render.cpp, render_perf.cpp, render_textures.cpp) stay its own.
- The key codes as our own constants (a platform/keys.h in place of lib/win_user.h's VK_ values in about 20 ui
  files), and `g_mainWindow` / the device's window as `platform::NativeWindow` (app.h, texture.h, the sound and
  renderer start-up): PL2a.
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
| Sound / video | Miles mss32 (47), DSOUND (1); Bink (12) | none since PL6 (null backends); A1 / A2 the real ones |
| Network | WSOCK32 (22, by ordinal) | BSD sockets map almost 1:1 (PL5) |
| Files, threads, time | KERNEL32 (159), WINMM (5), ADVAPI32 (11) | PL3, PL4; time done here |
