# Ticket queue

Small, self-contained work items for worker agents. One ticket per worker; a worker that finishes
reports and stops, and the next ticket goes to a fresh worker (docs/worker-brief.md is the common
part of every brief). A ticket should be doable in one sitting: one mismatch, one renderer step,
or 3-8 related functions. Mark a ticket taken by adding the date; remove it when merged.

## Renderer (src/render, docs/notes/renderer.md)

- R2b Picture windows on game memory: 004F0060 (makes and fills a picture surface: 32-bit copy,
  16-bit conversion through the device+0x4C converter) gives the surface game-owned memory under
  `cpuwindows`, so 005FCF80 / 005FD120 stop calling GetSurfaceDesc.
- R3 Save/load screen screenshot textures (made by 005FF0B0; locked by 00655550, 00655D40,
  00656464, 00656E10) on game memory.
- R5 The ~10 interface draws with old-device calls (list in renderer.md point 6), in groups of 3-4.
- R6 Movie surfaces and the software cursor's draws to the primary surface.
- R7 Holders with no DirectDraw surface at all (after R2/R3).
- R8 Device setup and texture management (last).

## Units and combat, 0x00500000-0x00540000 (docs/notes/units.md, combat.md)

- U2 done (00535850 ClientPlaceOnPath, 0052FCC0 / 0052FDF0 in follow-up for a crash).
- U3 (taken 2026-10-05) Next 3-5 largest live game-logic functions in the range (skip draws, matrix and bone code,
  effect/audio-adjacent 00534B60 / 00534760 / 00537500 until reviewed).

## App and startup, 0x00440000-0x00480000

- A2 (taken 2026-10-05) Network message objects over streams, first group: the classes with vtables
  0073C8A4 / 0073C8D8 / 0073C8F8 (constructors, read/write, destructors) in 00477E60-0047A6D0.
- A3 The rest of those message classes (vtables 0073C92C / 0073C990 / 0073C9C8 / 0073CA10).
- A4 The drivers at 0047B980-0047E340, in groups of 3-8.

## Interface, 0x005E0000-0x00640000 (docs/notes/ui.md)

- I1 Next 3-8 largest live non-rendering interface functions (the briefing screen excluded).

## Cleanup (docs/cleanup-plan.md)

- C1 (taken 2026-10-05) Tooling: `src/game/originals.h` generator and the artefact counts in `tools/progress.py`
  (phase 0). Routine; a cheaper model is fine.
- C2 Phase 1 for the engine base and core networking (0x00400000-0x00440000, fully ported): one
  real declaration per class, members instead of At<>/offsets, virtual calls instead of VCall,
  globals instead of addresses, duplicates merged; behaviour unchanged. Split per class family.
- X1 Small fixes: 0049B320 (unreferenced two-point line draw) to config/dead_code.csv; drop the
  false vtable entries 00747E2C (00504341 / 0050434F) from config/vtables.csv and functions.csv.
