# Ticket queue

Small, self-contained work items for worker agents. One ticket per worker; a worker that finishes
reports and stops, and the next ticket goes to a fresh worker (docs/worker-brief.md is the common
part of every brief). A ticket should be doable in one sitting: one mismatch, one renderer step,
or 3-8 related functions. Mark a ticket taken by adding the date; remove it when merged.

## Renderer (src/render, docs/notes/renderer.md)

- R3 (taken 2026-10-05) Save/load screen screenshot textures (made by 005FF0B0; locked by 00655550, 00655D40,
  00656464, 00656E10) on game memory.
- R5 The ~10 interface draws with old-device calls (list in renderer.md point 6), in groups of 3-4.
- R6 Movie surfaces and the software cursor's draws to the primary surface.
- R7 Holders with no DirectDraw surface at all (after R2/R3).
- R8 Device setup and texture management (last).

## Units and combat, 0x00500000-0x00540000 (docs/notes/units.md, combat.md)

- U2 done (00535850 ClientPlaceOnPath, 0052FCC0 / 0052FDF0 in follow-up for a crash).
- U6 Next 3-5 largest live game-logic functions in 0x00500000-0x00540000 (same skips; prefer ones a
  scenario reaches: combat, movement, units, map_objects).

## App and startup, 0x00440000-0x00480000


## Interface, 0x005E0000-0x00640000 (docs/notes/ui.md)

- I2 Next 3-8 largest live non-rendering interface functions (the briefing screen excluded).

## Cleanup (docs/cleanup-plan.md)

- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000, fully ported): one
  real declaration per class, members instead of At<>/offsets, virtual calls instead of VCall,
  globals instead of addresses, duplicates merged; behaviour unchanged. Split per class family:
  - C2c (taken 2026-10-05) Network sessions, connections and messages (net_*).
  - C2d Camera.
- C3 Merge duplicate forwarders using docs/originals-report.md (tools/gen_originals.py): the 80
  identical and 125 helper-name-only duplicates into shared declarations first; the 461 that
  differ need a decision per address. Routine for the first part (cheaper model).
- X1 Small fixes: 0049B320 (unreferenced two-point line draw) to config/dead_code.csv; drop the
  false vtable entries 00747E2C (00504341 / 0050434F) from config/vtables.csv and functions.csv.
- C4 Merge the stream duplicates C2b listed: BitStream (net_delta.h, same layout as CMemoryStream),
  ScriptStream / WorldStream, the CMemoryStream and root-chunk forwarders in net_link.cpp,
  world_game.cpp, world_zone.cpp, MapIterator / ObjectPair in base_manager.cpp, the registry view in
  hash_tables_400.cpp, and the ~40 literal uses of 0079B298.
- X3 ClientPlaceOnPath (00535850): 1 mismatch in 314,727 calls, a quaternion sign flip at a 180-degree
  turn ("q0 -0.707107 vs 0.707107", ui_hud): follow the original's branch for that edge case (plain
  C++ picks the other sign) or, if it's a rounding edge, document it; q and -q are the same rotation.
