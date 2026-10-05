# Ticket queue

Small, self-contained work items for worker agents. One ticket per worker; a worker that finishes
reports and stops, and the next ticket goes to a fresh worker (docs/worker-brief.md is the common
part of every brief). A ticket should be doable in one sitting: one mismatch, one renderer step,
or 3-8 related functions. Mark a ticket taken by adding the date; remove it when merged.

## Renderer (src/render, docs/notes/renderer.md)

- R5 (taken 2026-10-05) The ~10 interface draws with old-device calls (list in renderer.md point 6), in groups of 3-4.
- R6 Movie surfaces and the software cursor's draws to the primary surface.
- R7 Holders with no DirectDraw surface at all (after R2/R3).
- R8 Device setup and texture management (last).

## Units and combat, 0x00500000-0x00540000 (docs/notes/units.md, combat.md)

- U2 done (00535850 ClientPlaceOnPath, 0052FCC0 / 0052FDF0 in follow-up for a crash).

## App and startup, 0x00440000-0x00480000


## Interface, 0x005E0000-0x00640000 (docs/notes/ui.md)

- I7 Next 3-5 live interface functions (ui.md Fifteenth pass: 006379E0, 00637190 and the other battle commands, 006366F0, 0063DB20).

## HUD, battle screen, spells and items, 0x00640000-0x006C0000

- H4 Trader stock: no scenario reaches the stock generators (0069AE20, 0069A110, H3's 00699810 / 006995E0 / 00699A40 installed but 0 calls; ui_camp does not reach 004A6DF0). Find what calls 004A6DF0 and add a scenario; then 0069B5B0 (stock load), 0069A480 / 0069A780.

## Scripts, 0x004C0000-0x00500000 (docs/notes/script.md)

- S5 (taken 2026-10-05; 004C00D0, 004D3320, 004B3D10, 004B6330) Next script runtime functions (script.md "Blocks, areas and temporaries").

## Verify checks



## Cleanup (docs/cleanup-plan.md)

- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000, fully ported): one
  real declaration per class, members instead of At<>/offsets, virtual calls instead of VCall,
  globals instead of addresses, duplicates merged; behaviour unchanged. Split per class family:
  - (C2d camera merged 2026-10-05.)
- C3b (top 10, C3c and C3d, the next 15 by site count each, merged 2026-10-05; 482 left per the regenerated report, continue with the next 15 by site count) The 504 forwarders whose declarations differ between files (docs/originals-report.md "differ"
  group): decide per address which declaration is right (RET n, callers, Ghidra), keep one in the
  shared originals_<area>.h header. Start with the top 10 by site count (0x006E727B, 0x00455950,
  0x0040C720, ...), then by area. C3 first part (shared headers, 38 void forwarders typed) merged 2026-10-05.
- C4 Merge the stream duplicates C2b listed: BitStream (net_delta.h, same layout as CMemoryStream),
  ScriptStream / WorldStream, the CMemoryStream and root-chunk forwarders in net_link.cpp,
  world_game.cpp, world_zone.cpp, MapIterator / ObjectPair in base_manager.cpp, the registry view in
  hash_tables_400.cpp, and the ~40 literal uses of 0079B298.
