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
