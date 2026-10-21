# Application layer (0x00440000-0x00480000, outside the network code)

Startup, the frame step, registries, option and device records, key bindings, small tables. Network code of the same range is in
`network.md`. Names are provisional. Every function carries its address in its source comment (grep it). Most run at startup or in
every scenario.

- `app_frame.cpp`: WinMain's frame step `FrameStep` 00458010 (see "Frame capture"), intro movies 00458270 (list from the .ini, each
  played, the screen cleared by a Blt colour fill). `app_registry.cpp`: type registration with a factory (vtable <-> id <-> factory) and
  COM releases (00458570, 00458540, 00458550, 004586E0). `app_dxversion.cpp`: DirectX version probe 0045E310. `app_device_list.cpp`:
  device record list archiving (00463030-00465BC0).
- `map_spec_unit.cpp`: waypoint lists and behaviour records read / written as stream chunks (0045DE30 is a stream reader, not a
  CArray::SetSize; 0045DFB0, 0045D610, 0045D760). `zone_records.cpp`: zone exit destructors and the map database's zone record destructor
  (eight CStrings, exit CArray at +0x48; 00460460, 004608B0, 00460970).
- `object_tables.cpp`: `NamedFloatTable` (hash_map<CString, float>) Set 00460BC0 (zero removes; the value is tested with FCOMP against 0,
  so NaN removes too), Remove, CopyPrefixed, Load, Save (00461260). `core_streams.cpp`: `CStream::ReadString` 00461190 (bytes up to a
  zero into a CString; many callers keep forwarders). `key_bindings.cpp`: key bindings file load 00462C10 (key texts from
  `GetKeyNameTextA`, "command key" lines) and save 00462DF0; entries at 00787070 (code, name). `lever_table.cpp`: the atexit destructor
  00463120 of the lever table 0079C1A0 (vector of 0x2C records, hash map of vectors; it indexes the free lists with (0 - 1) >> 3 for a
  zero-sized record block with a non-null pointer). `string_map.cpp`: the debug information panel at 0079B930 (004447D0-00444F50: set,
  remove, tick, redraw, show, release). `options.cpp`: `GameOptions::SaveToRegistry` 0045C560 (EI_Plugin's `NewMaster` NOPs 42 bytes at
  0045D330 inside it: the Master Server Name is no longer saved).

## Frame capture (harness)

The harness's frame capture redirects the `CALL 005E65F0` at 004580D7 inside the original 00458010
(`tests/verify/harness_capture.cpp`). The rewritten `FrameStep` reads that call site's current target
from the rel32 each frame (005E65F0 when the byte is not a CALL) and calls it in the same place: after
the game's draw (EndScene) and the interface draw, before the cursor blit (005E54A0), the flip
(004EF610) and the lock's release (005E6610). The original body behind the hook's entry jump stays
untouched so the harness can still find the call. The camera-freeze skip (`CALL 0040FB30` at 00457595)
and the `PeekMessageA` return addresses (00457119 / 00457610) are in WinMain (00456440); the rewritten
WinMain keeps both working (A-E2 below). A harness hook point (ei/main_loop.h, S15a) for each of these
calls comes before the call-site read; the standalone image has only those (WinMain's rewrite reads no
call site since R9b2: game.exe runs only the original).

## Kept and skipped

- Kept: `FrameStep`'s movie capture passes two uninitialised stack words as the capture rectangle's size when there is no render
  target.
- Rebirth-patched: 00456440 (WinMain), 00443080, 00463390, 00468A00 are ported with the vanilla body and
  hooked in the vanilla launch only (A-E1, A-E2).
- Static initialisers (run before the hooks): 0045E820, 004632A0 (lever table constructor); 00444740 (debug
  panel) and 00458870 (GameOptions) are ported and compared at WinMain's entry (A-E2).
- Dead: 00442F00, the animation exporter's writer and six unreferenced functions.
- The fake unit scene loader (00471A90-004775F0, slot 62 of CFakeUnit via 00446BA0 -> 00446C10 ->
  00471B10) and 00477B00 (applies a unit record to a new unit, from 005927C0) are in `objects.md`.

## A-E1: the leftovers of 0x00440000-0x00480000

Small helpers (`app_helpers.cpp`, checks in `verify_app_helpers.cpp`; all on the main thread):

- 00455A90 `app::DiskFreeSpace` (path in ECX, a double in ST0): GetDiskFreeSpaceExA looked up in kernel32 on every call
  (GetModuleHandleA + GetProcAddress), its free-bytes-for-the-caller as a signed 64-bit value; without it
  sectors x bytes x free clusters (each zero-extended, `x87::FromUnsigned`); 0 when the call fails. Side by side; a
  difference is compared with a second run of the original (the free space can change). Save and load screens.
- 00455F10 `SetQuitAllowed` (CL to the byte 00784751), 00455F20 `QuitGame` (PostQuitMessage and 1 when the byte is set,
  else 0; ours only, it posts the message), 00455E20 `MovieMode` (the byte 00784752; side by side).
- 00455B50 `DelayedProtectionCheck` (CPlayerClient::OnNewObject calls it for every new object): rand() * 100 / 0x7FFF
  % 100 < 1, then the STLport list at 0079BC2C (built empty by the static initialiser 00455E50, freed by 00455F00) is
  searched for an entry whose byte is 0; one such entry runs 00455BA0 `ScrambleObjects` (the options back to their
  defaults, every object of the object manager 007AF5F8 given rand() * 0x1000 / 0x7FFF % 0x1000 as its companion
  (0050E8F0) and as its specific half (005161A0)). Nothing in the exe adds to the list, so the scramble never runs; both
  ours only (rand is drawn), the check wrapper also checks that the list is empty.
- 00455950 `CEngineObject::World` (the manager context's slot 13, `CManagerContext::ThisWorld`, now declared with its
  result; null without a context) side by side, on every unit's updates; 00457720 `CEngineObject::ReleaseAndDelete` (one
  reference less, `delete` at zero) ours only.
- 0045EA00 `CMapDatabase::LoadAll` ("map.txt", then "map-LMP.txt"; WinMain). Ours calls `Load` directly, so the loader's
  own check is no longer reached from it; `verify_map_database.cpp` runs the original's two loads on the shadow database
  and compares the databases in depth after `LoadAll`.
- 004645A0 `ChunkArchive::Destruct` (the out-of-line destructor the database loaders call; `DestructLocal`'s steps):
  ours only, both streams must carry the stream base's vtable afterwards.
- app_lifetime.cpp: the DirectInput mouse's destructor (004578D0: Shutdown, the two counted pointers'
  releases, the root vtable 0073C368; slot 0 004578B0) and the root class's slot 0 (00457950). Ours only; the
  application drops the mouse at exit. The party record and class template creators and the party manager's
  destructor in the same file are described in script.md.
- small_accessors.cpp: the counted pointer's destructor (00455C80, with the identical copies 00455610 and
  004BB050): one reference dropped (`--refCount_`, no delete), the pointer cleared. Ours only (its prologue is a
  short jump); the world server's tick and the object list iterators' owners call it. 00455610 is called by
  unwind funclets only.
- A-E1 library rows (config/library.csv, 60 rows; each row says why): STLport instantiations, MFC template helpers (element
  constructors / destructors of the CArray and CList instantiations at 004763C0-00477530 and others) and compiler-generated helpers
  only unwind funclets call.
- Rebirth-patched functions ported by A-E1 (vanilla behaviour, vanilla launch only): 00468A00 the lightning
  noise table's load (objects.md), 00463390 the lever table's load (units.md), 0040ECD0 the camera's path
  file load (core.md); and the network 0041AA10, 0041ACA0, 0041B1C0 (network.md).
- Not ported in 0x00400000-0x004C0000 after A-E1 (all but the last ported by A-E2, below): the Rebirth-patched 0041ECC0, 00443080 and 00456440
  (the application's start-up, 4832 bytes); the static initialisers 00444740 and 00458870 (they run
  before the hooks install); 004560D0 (the message box: it flips the DirectDraw surface to GDI); and 69
  functions under five bytes. 0049F7B1 is listed as a
  function but is the tail of `IsaacRandom::Seed` (0049F660, random.cpp) after its catch block (A-S11 gave it a
  `compiler` row in config/library.csv, so it is no longer counted).

## A-E2: WinMain, the static initialisers, the last Rebirth-patched functions, MFC TRY / CATCH

- **WinMain 00456440** (`app_main.cpp`, `app::GameMain`). It can be hooked: the hook DLL installs its hooks
  from a stub at WinMain's first instruction (hook.cpp), so a replacement of 00456440 itself takes effect when
  the stub jumps back. Rebirth patches the body (NivalRegistry, CommandLine, SavesPath, the CD check), so the
  rewrite runs in the vanilla launch only. Every callee is called through its address, so their hooks and
  wrappers keep running. Ours only (`verify_app_main.cpp`: no window at entry, the mouse object released at the
  end); every vanilla scenario runs on it from start to exit.
- The harness told the loop's calls apart by return address (the two `PeekMessageA` calls that take posted
  input, the frame's `timeGetTime`). The rewrite names the call it makes in `ei::mainloop::g_site`
  (`include/ei/main_loop.h`), which the harness accepts beside the addresses, and makes the frame's last
  camera update, the server's update and the client's tick through the harness's hook points when set
  (`g_harnessHooks`: "freeze" holds the camera, "speed" the updates), else directly.
- Start-up order: sysmsg.txt (three lines, built-in fallback), DirectX 7, the two class factories, the command
  line ("-moviepath_", "-nocam", .cam / .scr / .mob / .mpr, "-benchmark_", "-devicetarget_"), the window class
  (a running game is brought forward and this one exits), the registry root and options, the new handler, the
  worlds and the camera (field of view pi * 2 / 7), free disk space (300 MB warning) and the disc's label, the
  mouse object, the saves / mp / textures folders, the window, the renderer and the video memory flags
  (00784753-00784756), the sound, the intro movies, the databases, the cursors, the interface, the map list,
  the first map.
- The loop: the time since the last pass (at most 5 ticks; 1000 / fps while frames are recorded) is added to the
  server's and the client's backlog (0079BC64 / 0079BC68; the client's scaled by the client speed 00784758,
  dropped above 0x4000000). The server runs whole ticks; the client runs one while it has a tick and either
  server updates wait or its view is not in mode 2, and moves the camera a tick each time. Then the game
  flow's request, the tick fraction, the next client speed from balance = waiting updates - fraction
  (e^(balance / 10) when negative, 1 up to 1, else sqrt(balance) * 0.1 + 1 - 0.1; kept within 0.1 .. 10), the
  camera for the real time of the pass, the camera file's record / playback, the interface and a frame.
- Kept: a message that waits before the first pass goes to the interface's input or to the window procedure by
  the top byte of a reused stack slot (the disc folder's text pointer with sound, the free non-local video
  memory without); `SetUpCursors`' last argument has only its low byte set; "res\speech.res" is built twice.
- 00455A30 (`app::OutOfMemory`): the operator new handler WinMain installs ("Memory depleted", returns 0).
  Ghidra has no function there (config/extra_function_sizes.csv). Ours only, not reached.
- Static initialisers (they run before the hooks, so each port is compared at WinMain's entry with what the
  original built: `verify_static_initialisers.cpp`): the debug panel 00444740, the options 00458870
  (`options_construct.cpp`; the layout is named in options.h), `CSysInfo`'s unit 004DDCB0-004DE87C
  (`sys_info.cpp`) and the weather's noise curves 004E3980 (`weather_noise.cpp`: rebuilt from `rand` seeded
  with 1, bit for bit).
- 004560D0 `ShowMessageBox` (full screen: DirectDraw flips to the GDI surface first; the cursor is put back).
  Ours only. 006462A0, the unit panel's draw: items_spells.md.
- **MFC TRY / CATCH** (`src/lib/mfc_try.h`): the game's MFC throws C++ exceptions (a `CException*`), and a
  TRY block is an `AFX_EXCEPTION_LINK` (007020F1; its destructor is AfxTryCleanup 0070212F) around a
  try / catch. A rewrite writes exactly that: `mfc::ExceptionLink` and a C++ handler for a global class with
  the game's name (`CException`, `CFileException`); this compiler's frame handler matches what the game's
  _CxxThrowException raises by the decorated type name, and the game's frames in between unwind through their
  own handlers. Ported with it: 004D5880 (the script loader's text file reader, `script_text_file.cpp`),
  `CDib::Read` 006D5910 / `Write` 006D5AC0 (`dib_file.cpp`). No scenario reaches them; a start-up check
  (`verify_mfc_try.cpp`) drives them side by side with the originals: a missing text file (the throw from the
  game's CStdioFile constructor is caught, the TRY chain is as before), a text file with a line longer than
  the 0x800 buffer, bitmaps of 16 / 24 / 32 bits written and read back, 8 bit files with a colour table. The
  CDib handlers (a message box) are not driven. 004F0B90 can be ported the same way.
