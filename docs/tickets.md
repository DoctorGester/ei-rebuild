# Ticket queue

Small, self-contained work items for worker agents. One ticket per worker; a worker that finishes
reports and stops, and the next ticket goes to a fresh worker (docs/worker-brief.md is the common
part of every brief). A ticket should be doable in one sitting: one mismatch, one renderer step,
or 3-8 related functions. Mark a ticket taken by adding the date; remove it when merged.

## Renderer (src/render, docs/notes/renderer.md)

- R6 Movie surfaces and the software cursor's draws to the primary surface.
- R7 Holders with no DirectDraw surface at all (after R2/R3).
- R8 Device setup and texture management (last).

## Units and combat, 0x00500000-0x00540000 (docs/notes/units.md, combat.md)

- U2 done (00535850 ClientPlaceOnPath, 0052FCC0 / 0052FDF0 in follow-up for a crash).

## App and startup, 0x00440000-0x00480000


## Interface, 0x005E0000-0x00640000 (docs/notes/ui.md)

- I9 (taken 2026-10-05) UseAtPoint 00637750 has 0 calls (ui_battle_spells: the spells chosen all target units?); quick items on a target (mode 3) and 0063DB20 need a scenario with a wand or scroll in a quick slot.

## HUD, battle screen, spells and items, 0x00640000-0x006C0000

- H6 Small leftovers in 0x00690000-0x006A0000: 00699410, GenerateItems thunks 0069B570 / 0069B550 / 0069B590 (H5 notes).

## Scripts, 0x004C0000-0x00500000 (docs/notes/script.md)


## Verify checks

- X10 (taken 2026-10-05) First real bugs from other zones (X9 merged: zone/waitzone steps; zone_base_bz1g and zone_gz3g pass). In gz2g with hooks: `@AddUnitToServer("TrapWolf","WolfTutor",...)` creates no unit (passes with no hooks), and the ScriptRun::Execute command check mismatches on command 8 (2 of 17566).



## Cleanup (docs/cleanup-plan.md)

- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000, fully ported): one
  real declaration per class, members instead of At<>/offsets, virtual calls instead of VCall,
  globals instead of addresses, duplicates merged; behaviour unchanged. Split per class family:
  - (C2d camera merged 2026-10-05.)
- C3b (top 10, C3c-C3e and C3f, the next 15 by site count each; C3g merged; C3h done on its branch, 434 left per the regenerated report, continue with the next 15 by site count) The 504 forwarders whose declarations differ between files (docs/originals-report.md "differ"
  group): decide per address which declaration is right (RET n, callers, Ghidra), keep one in the
  shared originals_<area>.h header. Start with the top 10 by site count (0x006E727B, 0x00455950,
  0x0040C720, ...), then by area. C3 first part (shared headers, 38 void forwarders typed) merged 2026-10-05.
- C4 (merge reverted 2026-10-05: BitStream as CMemoryStream broke all Pack/Send byte checks; the worker is fixing on its branch) Merge the stream duplicates C2b listed: BitStream (net_delta.h, same layout as CMemoryStream),
  ScriptStream / WorldStream, the CMemoryStream and root-chunk forwarders in net_link.cpp,
  world_game.cpp, world_zone.cpp, MapIterator / ObjectPair in base_manager.cpp, the registry view in
  hash_tables_400.cpp, and the ~40 literal uses of 0079B298.
