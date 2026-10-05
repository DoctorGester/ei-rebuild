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
- U7 (taken 2026-10-05) Next 3-5 largest live game-logic functions in 0x00500000-0x00540000 (same skips).

## App and startup, 0x00440000-0x00480000


## Interface, 0x005E0000-0x00640000 (docs/notes/ui.md)

- I3 Next 3-8 interface functions; candidates from ui.md "Tenth pass": 00610AD0, 00611220, 00634790, 0062B7D0, 00632D70 (add scenario steps where needed).

## Cleanup (docs/cleanup-plan.md)

- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000, fully ported): one
  real declaration per class, members instead of At<>/offsets, virtual calls instead of VCall,
  globals instead of addresses, duplicates merged; behaviour unchanged. Split per class family:
  - C2e (taken 2026-10-05) Network message classes, one class per message (net_message_classes.cpp,
    net_messages.cpp, message offsets in net_server_* / net_client_*); docs/notes/cleanup-network.md.
  - C2f Network links (net_link.*, net_session_pump.cpp): VCalls, duplicate layouts, FileStream from CStream.
  - C2d Camera.
- C3 Merge duplicate forwarders using docs/originals-report.md (tools/gen_originals.py): the 80
  identical and 125 helper-name-only duplicates into shared declarations first; the 461 that
  differ need a decision per address. Routine for the first part (cheaper model).
- C4 Merge the stream duplicates C2b listed: BitStream (net_delta.h, same layout as CMemoryStream),
  ScriptStream / WorldStream, the CMemoryStream and root-chunk forwarders in net_link.cpp,
  world_game.cpp, world_zone.cpp, MapIterator / ObjectPair in base_manager.cpp, the registry view in
  hash_tables_400.cpp, and the ~40 literal uses of 0079B298.
