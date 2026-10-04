# Ticket queue

Small, self-contained work items for worker agents. One ticket per worker; a worker that finishes
reports and stops, and the next ticket goes to a fresh worker (docs/worker-brief.md is the common
part of every brief). A ticket should be doable in one sitting: one mismatch, one renderer step,
or 3-8 related functions. Mark a ticket taken by adding the date; remove it when merged.

## Renderer (src/render, docs/notes/renderer.md)

- R2 Window surfaces without DirectDraw under `present`: creation/release (004EF7B0 / 004EFD20)
  and 005FCF80 / 005FD120 (GetSurfaceDesc) on game-owned memory only.
- R3 Console pixel access on game-owned memory: 00655550, 00655D40, 00656464, 00656E10.
- R4 Render-state presets fed to `render::state`: 0049B140, 0049B4B0, 00501C60, 0050434F, 005045D0.
- R5 The ~10 interface draws with old-device calls (list in renderer.md point 6), in groups of 3-4.
- R6 Movie surfaces and the software cursor's draws to the primary surface.
- R7 Holders with no DirectDraw surface at all (after R2/R3).
- R8 Device setup and texture management (last).

## Units and combat, 0x00500000-0x00540000 (docs/notes/units.md, combat.md)

- U2 Resistances 00535850 and 0052FDF0.
- U3 Next 3-5 largest live game-logic functions in the range (skip draws, matrix and bone code,
  effect/audio-adjacent 00534B60 / 00534760 / 00537500 until reviewed).

## App and startup, 0x00440000-0x00480000

- A2.. To be split after the current worker's report (largest remaining live functions, 3-8 each).

## Interface, 0x005E0000-0x00640000 (docs/notes/ui.md)

- I1 Next 3-8 largest live non-rendering interface functions (the briefing screen excluded).

## Cleanup (docs/cleanup-plan.md)

- C1 Tooling: `src/game/originals.h` generator and the artefact counts in `tools/progress.py`
  (phase 0). Routine; a cheaper model is fine.
