# Application layer (0x00440000-0x00480000, outside the network code)

Startup, the frame step, registries, option and device records, key bindings, small tables. Network
code of the same range is in `network.md`. Names are provisional. Verify files are named after the
source files (`verify_app_registry`, `verify_app_dxversion`, `verify_app_device_list`,
`verify_string_map`, `verify_named_float_table`, `verify_key_bindings`); most functions run at startup
or in every scenario.

| Address | Name | File | Verify |
|---|---|---|---|
| 00458010 | WinMain's frame step (`FrameStep`) | `app_frame.cpp` | see "Frame capture" |
| 00458270 | intro movies (list from the .ini, each played, the screen cleared by a Blt colour fill) | `app_frame.cpp` | ours |
| 00458570, 00458540, 00458550, 004586E0 | type registration with a factory (vtable <-> id <-> factory) and COM releases | `app_registry.cpp` | ours |
| 0045E310 | DirectX version probe | `app_dxversion.cpp` | ours |
| 00463030, 004652A0, 004657E0, 00465A70, 00465BC0, 00465090 | device record list archiving and the record's members | `app_device_list.cpp` | ours |
| 0045DE30, 0045DFB0, 0045D610, 0045D760 | waypoint lists and behaviour records read / written (stream chunks; 0045DE30 is a stream reader, not a CArray::SetSize) | `map_spec_unit.cpp` | ours |
| 00460460, 004608B0, 00460970 | zone exit destructors (`ZoneExit`) and the map database's zone record destructor (eight CStrings, exit CArray at +0x48) | `zone_records.cpp` | ours |
| 00460BC0, 00460DB0, 00460EE0, 00460FE0, 00461260 | `NamedFloatTable` (hash_map<CString, float>): Set (zero removes), Remove, CopyPrefixed, Load, Save | `object_tables.cpp` | ours only: the name holds the value or is gone; CopyPrefixed copies exactly the prefixed entries |
| 00461190 | `CStream::ReadString` (bytes up to a zero into a CString; many callers keep forwarders) | `core_streams.cpp` | |
| 00462C10, 00462DF0 | key bindings file load (key texts from `GetKeyNameTextA`, "command key" lines) and save; entries at 00787070 (code, name) | `key_bindings.cpp` | the original loads first, bindings and key texts compared after ours loads the same file |
| 00463120 | atexit destructor of the lever table 0079C1A0 (vector of 0x2C records, hash map of vectors) | `lever_table.cpp` | ours |
| 004447D0-00444F50 (10) | debug information panel at 0079B930 (set, remove, tick, redraw, show, release) | `string_map.cpp` | ours |
| 0045C560 | `GameOptions::SaveToRegistry` (EI_Plugin's `NewMaster` NOPs 42 bytes at 0045D330 inside it: the Master Server Name is no longer saved) | `options.cpp` | |

## Frame capture (harness)

The harness's frame capture redirects the `CALL 005E65F0` at 004580D7 inside the original 00458010
(`tests/verify/harness_capture.cpp`). The rewritten `FrameStep` reads that call site's current target
from the rel32 each frame (005E65F0 when the byte is not a CALL) and calls it in the same place: after
the game's draw (EndScene) and the interface draw, before the cursor blit (005E54A0), the flip
(004EF610) and the lock's release (005E6610). The original body behind the hook's entry jump stays
untouched so the harness can still find the call. The camera-freeze skip (`CALL 0040FB30` at 00457595)
and the `PeekMessageA` return addresses (00457119 / 00457610) are in WinMain (00456440).

## Kept and skipped

- Kept: `NamedFloatTable::Set` tests the value with FCOMP against 0 (NaN removes the entry like 0);
  `FrameStep`'s movie capture passes two uninitialised stack words as the capture rectangle's size when there
  is no render target; the lever table's destructor indexes the free lists with (0 - 1) >> 3 for a
  zero-sized record block with a non-null pointer.
- Rebirth-patched, not rewritten: 00456440 (WinMain: NivalRegistry, the harness keys on its return
  addresses), 00443080, 00463390, 00468A00.
- Static initialisers (run before the hooks): 00444740 (debug panel), 00458870 (GameOptions), 0045E820,
  004632A0 (lever table constructor).
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
- A-E1 library rows (config/library.csv, 60 rows; each row says why): STLport instantiations (hash map
  operator[] 00464750, node deallocators 00464730 / 00499800, list and pair destructors 0049C6E0, 004824B0,
  00483D40, 00464DF0, the aligned allocators and vector helpers 004A2BF0, 004A3090, 004A3060, 004A2FC0,
  00455890, 00482DD0, 004991C0, 004991E0, 00482790), MFC template helpers (the element constructors /
  destructors / zero fills of the CArray and CList instantiations at 004763C0-00477530, 00499350, 0049BB10,
  00475F20, 00498C80, 00498E30, 004553F0, 00462230, the CString thunks 004563A0 and 004B9F20, 0049F810) and
  compiler-generated helpers (the four-dword buffer constructors 00474C70 / 00475F10 / 00498E20, the pointer
  holder constructors 004983F0 / 004BB040, operator= 00474C50, the delete wrapper 00462520 and the helpers
  only unwind funclets call: 00445FC0, 00481E50, 00447F40, 0047A3D0, 0047A3E0, 00455820, 00462610, 00482E90,
  00498E50, 00457940, 004688F0, 00481560).
- Rebirth-patched functions ported by A-E1 (vanilla behaviour, vanilla launch only): 00468A00 the lightning
  noise table's load (objects.md), 00463390 the lever table's load (units.md).
- Not ported in 0x00400000-0x004C0000 after A-E1: the Rebirth-patched 0040ECD0, 0041ECC0, 00443080, 00456440 and the
  network 0041AA10, 0041ACA0, 0041B1C0 (vanilla launch only); the static initialisers 00444740, 00458870,
  0045E820, 004632A0, 004A5870 (they run
  before the hooks install); 004560D0 (the message box: it flips the DirectDraw surface to GDI); the unit
  slots 004467C0, 00446810, 00446870 (objects.md); and 69 functions under five bytes. 0049F7B1 is listed as a
  function but is the tail of `IsaacRandom::Seed` (0049F660, random.cpp) after its catch block.
